/*
 * the older url engine: splits a url into scheme, server, path segments, query and fragment, and rebuilds it after
 * resolving dots. it handles what the newer engine gives up on, and UrlEscape, UrlGetPart, UrlIs, UrlCreateFromPath
 * and PathCreateFromUrl use it.
 */
#include "url_private.h"

/* ------------------------------------------------------------------ text */

void KbUrlTextInit(URL_TEXT *t)
{
    t->text = nullptr;
    t->length = t->capacity = 0;
    t->failed = FALSE;
}

void KbUrlTextFree(URL_TEXT *t)
{
    KbFree(t->text);
    KbUrlTextInit(t);
}

static BOOL text_reserve(URL_TEXT *t, SIZE_T extra)
{
    if (t->failed)
        return FALSE;
    if (t->length + extra + 1 <= t->capacity)
        return TRUE;
    SIZE_T capacity = t->capacity ? t->capacity : 64;
    while (capacity < t->length + extra + 1)
        capacity *= 2;
    LPWSTR text = KbAlloc(capacity * sizeof(WCHAR));
    if (!text) {
        t->failed = TRUE;
        return FALSE;
    }
    if (t->text)
        memcpy(text, t->text, (t->length + 1) * sizeof(WCHAR));
    else
        text[0] = 0;
    KbFree(t->text);
    t->text = text;
    t->capacity = capacity;
    return TRUE;
}

void KbUrlTextAppend(URL_TEXT *t, LPCWSTR s, SIZE_T length)
{
    if (!s || !text_reserve(t, length))
        return;
    memcpy(t->text + t->length, s, length * sizeof(WCHAR));
    t->length += length;
    t->text[t->length] = 0;
}

void KbUrlTextAppendChar(URL_TEXT *t, WCHAR c)
{
    KbUrlTextAppend(t, &c, 1);
}

static void text_append_string(URL_TEXT *t, LPCWSTR s)
{
    if (s)
        KbUrlTextAppend(t, s, wcslen(s));
}

void KbUrlTextSet(URL_TEXT *t, LPCWSTR s)
{
    t->length = 0;
    if (t->text)
        t->text[0] = 0;
    if (!text_reserve(t, 0))
        return;
    text_append_string(t, s);
}

static LPCWSTR text_string(URL_TEXT *t)
{
    return t->text ? t->text : L"";
}

HRESULT KbUrlCopyOut(LPCWSTR text, LPWSTR out, DWORD *size)
{
    SIZE_T length = wcslen(text);
    if (*size <= length || !out) {
        *size = (DWORD)length + 1;
        return E_POINTER;
    }
    memcpy(out, text, (length + 1) * sizeof(WCHAR));
    *size = (DWORD)length;
    return S_OK;
}

HRESULT KbUrlCopyOutA(LPCWSTR text, LPSTR out, DWORD *size)
{
    int needed = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return E_FAIL;
    if (*size < (DWORD)needed || !out) {
        *size = (DWORD)needed;
        return E_POINTER;
    }
    WideCharToMultiByte(CP_ACP, 0, text, -1, out, (int)*size, nullptr, nullptr);
    *size = (DWORD)needed - 1;
    return S_OK;
}

/* ------------------------------------------------------------------ small helpers */

static BOOL is_drive(LPCWSTR p)
{
    return (KbIsDriveLetter(p[0]) && p[1] == L':') || (p[0] && p[1] == L'|');
}

static BOOL is_dot_dot(LPCWSTR s)
{
    return s[0] == L'.' && s[1] == L'.' && !s[2];
}

static void lower_ascii(LPWSTR s)
{
    for (; s && *s; s++)
        if (*s >= L'A' && *s <= L'Z')
            *s += 32;
}

/* in a file url, "#" only starts a fragment after one of these extensions. */
static const PCWSTR fragment_extensions[] = {L".html", L".htm", L".xml",  L".doc",  L".xls",  L".ppt",  L".rtf",
                                             L".dot",  L".xlw", L".pps",  L".xlt",  L".hta",  L".pot",  L".pdf",
                                             L".xps",  L".xbap", L".xlsx", L".pptx", L".docx"};

static BOOL ends_with_extension(LPCWSTR start, LPCWSTR at)
{
    SIZE_T before = (SIZE_T)(at - start);
    for (ULONG i = 0; i < RTL_NUMBER_OF(fragment_extensions); i++) {
        SIZE_T length = wcslen(fragment_extensions[i]);
        if (length <= before && !_wcsnicmp(at - length, fragment_extensions[i], length))
            return TRUE;
    }
    return FALSE;
}

LPCWSTR KbUrlFindFragment(LPCWSTR url, BOOL file)
{
    if (!url)
        return nullptr;
    LPCWSTR hash = wcschr(url, L'#');
    if (!hash || !file)
        return hash;
    LPCWSTR question = wcschr(url, L'?');
    if (question && question < hash)
        return hash;
    for (; hash; hash = wcschr(hash + 1, L'#'))
        if (ends_with_extension(url, hash))
            return hash;
    return nullptr;
}

/* ------------------------------------------------------------------ breaking */

static void add_segments(LPWSTR path, URL_PARTS *parts)
{
    UINT count = 1;
    for (LPWSTR p = path; *p; p++)
        if (*p == L'/')
            count++;
    parts->path = KbAllocZero(count * sizeof(URL_SEGMENT));
    if (!parts->path)
        return;
    parts->count = 0;
    LPWSTR start = path;
    for (LPWSTR p = path;; p++) {
        if (*p == L'/' || !*p) {
            BOOL end = !*p;
            *p = 0;
            parts->path[parts->count++].text = start;
            if (end)
                break;
            start = p + 1;
        }
    }
    LPWSTR last = parts->path[parts->count - 1].text;
    if (!*last) {
        if (parts->count > 1)
            parts->count--;
        parts->flags |= PART_DIRECTORY;
    } else if ((last[0] == L'.' && !last[1]) || is_dot_dot(last)) {
        parts->flags |= PART_DIRECTORY;
    }
}

/* the server depends on the slash count. at points at the first slash. */
static void file_server(LPWSTR *at, URL_PARTS *parts)
{
    LPWSTR p = *at, last = p;
    int slashes = 0;
    while (*p == L'/' || *p == L'\\') {
        last = p++;
        slashes++;
    }
    if (!slashes && !is_drive(p))
        return;
    parts->flags |= PART_ROOTED;
    if (!slashes)
        return;
    switch (slashes) {
    case 4:
        parts->flags |= PART_UNC4;
        [[fallthrough]];
    case 2:
        if (is_drive(last + 1)) {
            *last = 0;
            parts->server = last;
            *at = last + 1;
            parts->flags |= PART_UNC4;
            break;
        }
        [[fallthrough]];
    case 5:
    case 6: {
        LPWSTR server = last + 1, end = server;
        parts->server = server;
        while (*end && *end != L'/' && *end != L'\\')
            end++;
        if (*end)
            *end++ = 0;
        *at = end;
        break;
    }
    default:
        *last = 0;
        parts->server = last;
        *at = last + 1;
        break;
    }
    if (parts->server && !_wcsicmp(parts->server, L"localhost"))
        parts->server = nullptr;
}

void KbUrlBreak(LPWSTR url, URL_PARTS *parts)
{
    memset(parts, 0, sizeof(*parts));
    if (!*url) {
        parts->flags = PART_EMPTY;
        return;
    }
    LPWSTR p = url;
    if (!is_drive(p)) {
        SIZE_T length = KbUrlSchemeLength(p);
        if (length) {
            p[length] = 0;
            lower_ascii(p);
            parts->scheme = p;
            DWORD flags;
            parts->type = KbUrlSchemeType(p, length, &flags);
            parts->flags = flags & 0xFF;
            p += length + 1;
        }
    }
    if (!*p)
        return;
    if (!(parts->flags & PART_OPAQUE)) {
        LPWSTR hash = (LPWSTR)KbUrlFindFragment(p, parts->type == URL_SCHEME_FILE);
        if (hash) {
            *hash = 0;
            parts->fragment = hash + 1;
        }
        LPWSTR question = wcschr(p, L'?');
        if (!question && parts->fragment)
            question = wcschr(parts->fragment, L'?');
        if (question) {
            *question = 0;
            parts->query = question + 1;
        }
        if (parts->flags & PART_SLASHES)
            for (LPWSTR q = p; *q; q++)
                if (*q == L'\\')
                    *q = L'/';
        if (parts->type == URL_SCHEME_FILE) {
            file_server(&p, parts);
        } else if (parts->type == URL_SCHEME_MK) {
            if (*p == L'@') {
                parts->server = p;
                LPWSTR slash = wcschr(p, L'/');
                if (slash) {
                    parts->flags |= PART_ROOTED;
                    *slash = 0;
                    p = slash + 1;
                } else {
                    p += wcslen(p);
                }
            }
        } else if (*p == L'/') {
            parts->flags |= PART_ROOTED;
            p++;
            if (*p == L'/') {
                parts->server = ++p;
                LPWSTR slash = wcschr(p, L'/');
                if (slash) {
                    *slash = 0;
                    p = slash + 1;
                } else {
                    p += wcslen(p);
                }
            }
        } else if (parts->scheme) {
            parts->flags |= PART_OPAQUE;
        }
    }
    if (!*p)
        return;
    if (parts->flags & PART_OPAQUE) {
        parts->path = KbAllocZero(sizeof(URL_SEGMENT));
        if (parts->path) {
            parts->path[0].text = p;
            parts->count = 1;
        }
        return;
    }
    if (!parts->server && (*p == L'/' || *p == L'\\')) {
        parts->flags |= PART_ROOTED;
        p++;
    }
    if (!*p)
        return;
    if (is_drive(p))
        parts->flags |= PART_DRIVE;
    add_segments(p, parts);
}

void KbUrlPartsFree(URL_PARTS *parts)
{
    KbFree(parts->path);
    KbFree(parts->more);
    memset(parts, 0, sizeof(*parts));
}

/* ------------------------------------------------------------------ canonical parts */

static int last_live(URL_SEGMENT *segments, UINT count)
{
    for (int i = (int)count - 1; i >= 0; i--)
        if (!segments[i].dead)
            return i;
    return -1;
}

/* "." goes, ".." removes the live segment before it unless that is ".." itself or a drive. */
static void canon_segments(URL_SEGMENT *segments, UINT count, BOOL drive)
{
    for (UINT i = 0; i < count; i++) {
        if (segments[i].dead)
            continue;
        LPCWSTR text = segments[i].text;
        if (text[0] == L'.' && !text[1]) {
            segments[i].dead = TRUE;
            continue;
        }
        if (!is_dot_dot(text))
            continue;
        int previous = last_live(segments, i);
        if (previous < 0 || is_dot_dot(segments[previous].text) || (previous == 0 && drive))
            continue;
        segments[previous].dead = TRUE;
        segments[i].dead = TRUE;
    }
}

/* leading ".." segments of a relative path climb the base path and vanish above its root. */
static void canon_combine(URL_PARTS *parts)
{
    for (UINT i = 0; i < parts->more_count; i++) {
        if (parts->more[i].dead)
            continue;
        if (!is_dot_dot(parts->more[i].text))
            return;
        int last = last_live(parts->path, parts->count);
        if (last >= 0 && !(last == 0 && (parts->flags & PART_DRIVE)))
            parts->path[last].dead = TRUE;
        parts->more[i].dead = TRUE;
    }
}

static const PCWSTR default_ports[][2] = {
    {L"ftp", L":21"}, {L"http", L":80"}, {L"gopher", L":70"}, {L"https", L":443"}};

static void canon_server(URL_PARTS *parts)
{
    if (!parts->server || !(parts->flags & PART_SERVER))
        return;
    LPWSTR host = wcsrchr(parts->server, L'@');
    host = host ? host : parts->server;
    for (LPWSTR p = host; *p; p++)
        *p = KbDowncase(*p);
    LPWSTR colon = wcschr(host, L':');
    if (!colon)
        return;
    for (ULONG i = 0; i < RTL_NUMBER_OF(default_ports); i++)
        if (parts->scheme && !wcscmp(parts->scheme, default_ports[i][0]) && !wcscmp(colon, default_ports[i][1]))
            *colon = 0;
}

static void canon_parts(URL_PARTS *parts)
{
    canon_server(parts);
    canon_segments(parts->path, parts->count, (parts->flags & PART_DRIVE) != 0);
    canon_segments(parts->more, parts->more_count, FALSE);
    if (parts->more_count)
        canon_combine(parts);
}

/* ------------------------------------------------------------------ building */

static BOOL has_live(URL_SEGMENT *segments, UINT count)
{
    return last_live(segments, count) >= 0;
}

/* joins live segments with "/", starting with one when leading. returns whether the last was empty. */
static BOOL build_segments(URL_SEGMENT *segments, UINT count, URL_TEXT *out, BOOL leading)
{
    BOOL first = TRUE, empty = FALSE;
    for (UINT i = 0; i < count; i++) {
        if (segments[i].dead)
            continue;
        if (leading || !first)
            KbUrlTextAppendChar(out, L'/');
        first = FALSE;
        empty = !*segments[i].text;
        text_append_string(out, segments[i].text);
    }
    return empty;
}

static void build_path(URL_PARTS *parts, URL_TEXT *out)
{
    BOOL trailing = FALSE;
    if (parts->count) {
        trailing = build_segments(parts->path, parts->count, out, (parts->flags & PART_ROOTED) != 0);
        if (trailing)
            KbUrlTextAppendChar(out, L'/');
    }
    if (parts->more_count) {
        BOOL leading = !trailing && (has_live(parts->path, parts->count) || (parts->flags & PART_ROOTED));
        if (build_segments(parts->more, parts->more_count, out, leading)) {
            KbUrlTextAppendChar(out, L'/');
            trailing = TRUE;
        }
    }
    if (trailing)
        return;
    if (!(parts->flags & PART_DIRECTORY)) {
        if (has_live(parts->path, parts->count) || has_live(parts->more, parts->more_count))
            return;
        if (!(parts->flags & PART_ROOTED))
            return;
    }
    KbUrlTextAppendChar(out, L'/');
}

static void build_server(URL_PARTS *parts, DWORD flags, URL_TEXT *out)
{
    if (parts->type == URL_SCHEME_FILE) {
        if (!(flags & (URL_WININET_COMPATIBILITY | URL_FILE_USE_PATHURL))) {
            if (parts->flags & PART_ROOTED)
                text_append_string(out, L"//");
        } else if (parts->server && *parts->server) {
            text_append_string(out, L"////");
        } else if (parts->count && is_drive(parts->path[0].text)) {
            KbUrlTextAppendChar(out, L'/');
        } else if (parts->flags & PART_ROOTED) {
            text_append_string(out, L"//");
        }
    } else if (parts->type != URL_SCHEME_MK && parts->server) {
        text_append_string(out, L"//");
    }
    text_append_string(out, parts->server);
}

void KbUrlBuild(URL_PARTS *parts, DWORD flags, URL_TEXT *out)
{
    if (parts->scheme) {
        text_append_string(out, parts->scheme);
        KbUrlTextAppendChar(out, L':');
    }
    build_server(parts, flags, out);
    build_path(parts, out);
    if (parts->query) {
        KbUrlTextAppendChar(out, L'?');
        text_append_string(out, parts->query);
    }
    if (parts->fragment) {
        KbUrlTextAppendChar(out, L'#');
        text_append_string(out, parts->fragment);
    }
}

/* wininet keeps a dos path with backslashes after "file://". */
static void wininet_slashes(LPWSTR url)
{
    if (!url || wcslen(url) <= 7)
        return;
    for (LPWSTR p = url + 7; *p; p++)
        if (*p == L'/')
            *p = L'\\';
}

/* an optional "//server", then the path with backslashes, unescaped unless the url had four
   slashes or URL_DONT_UNESCAPE is set. */
static HRESULT build_dos_path(URL_PARTS *parts, URL_TEXT *out, DWORD flags, BOOL wininet)
{
    if (parts->count && is_drive(parts->path[0].text))
        parts->flags &= ~PART_ROOTED;
    if (parts->server && !*parts->server)
        parts->server = nullptr;
    parts->type = URL_SCHEME_UNKNOWN;
    if (parts->server) {
        text_append_string(out, L"//");
        text_append_string(out, parts->server);
    }
    if (parts->count)
        build_path(parts, out);
    if (out->failed)
        return E_OUTOFMEMORY;
    LPWSTR text = out->text ? out->text : (LPWSTR)L"";
    if (wininet) {
        wininet_slashes(out->text);
    } else {
        for (LPWSTR p = out->text; p && *p && *p != L'?' && *p != L'#'; p++)
            if (*p == L'/')
                *p = L'\\';
    }
    HRESULT hr = S_OK;
    if (!(parts->flags & PART_UNC4) && !(flags & URL_DONT_UNESCAPE) && out->text) {
        hr = KbUrlUnescapeInPlace(out->text, flags);
        out->length = wcslen(out->text);
    }
    text = out->text;
    if (SUCCEEDED(hr) && text && text[0] && text[1] == L'|')
        text[1] = L':';
    return hr;
}

/* ------------------------------------------------------------------ escaping */

/* shared by UrlEscape and path escaping. */
static void escape_char(URL_TEXT *out, LPCWSTR *at, DWORD flags)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    LPCWSTR p = *at;
    WCHAR c = *p;
    if ((flags & URL_ESCAPE_AS_UTF8) && c >= 0x80) {
        int units = c >= 0xD800 && c <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF ? 2 : 1;
        char bytes[6];
        int count = WideCharToMultiByte(CP_UTF8, 0, p, units, bytes, 6, nullptr, nullptr);
        for (int i = 0; i < count; i++) {
            BYTE b = (BYTE)bytes[i];
            WCHAR esc[3] = {L'%', hex[b >> 4], hex[b & 15]};
            KbUrlTextAppend(out, esc, 3);
        }
        *at += units;
        return;
    }
    BOOL escape = FALSE;
    if ((flags & URL_ESCAPE_ASCII_URI_COMPONENT) && c && c < 0x80 && !KbUrlClass(c, URLC_ALNUM | URLC_UNRESERVED))
        escape = TRUE;
    else if (c <= 0xFF && !KbUrlClass(c, URLC_ALNUM | URLC_SAFE))
        escape = TRUE;
    else if ((flags & URL_ESCAPE_PERCENT) && c == L'%')
        escape = TRUE;
    if (escape) {
        WCHAR esc[3] = {L'%', hex[(c >> 4) & 15], hex[c & 15]};
        KbUrlTextAppend(out, esc, 3);
    } else {
        KbUrlTextAppendChar(out, c);
    }
    (*at)++;
}

static void escape_string(URL_TEXT *out, LPCWSTR s, DWORD flags)
{
    while (s && *s)
        escape_char(out, &s, flags);
}

/* escapes the live segments into store and points the parts at it. */
static BOOL escape_segments(URL_PARTS *parts, DWORD flags, URL_TEXT *store)
{
    URL_TEXT pieces[1];
    KbUrlTextInit(pieces);
    for (UINT i = 0; i < parts->count; i++) {
        if (parts->path[i].dead)
            continue;
        escape_string(pieces, parts->path[i].text, flags);
        KbUrlTextAppendChar(pieces, L'\x1');
    }
    if (pieces->failed) {
        KbUrlTextFree(pieces);
        return FALSE;
    }
    *store = *pieces;
    UINT n = 0;
    LPWSTR p = store->text;
    for (UINT i = 0; i < parts->count && p; i++) {
        if (parts->path[i].dead)
            continue;
        LPWSTR end = wcschr(p, L'\x1');
        *end = 0;
        parts->path[n].text = p;
        parts->path[n].dead = FALSE;
        n++;
        p = end + 1;
    }
    parts->count = n;
    return TRUE;
}

HRESULT KbUrlEscape(LPCWSTR url, URL_TEXT *out, DWORD flags)
{
    if (!url)
        return E_INVALIDARG;
    KbUrlTextSet(out, L"");
    if (flags & URL_ESCAPE_SPACES_ONLY) {
        for (; *url; url++) {
            if (*url == L' ')
                text_append_string(out, L"%20");
            else
                KbUrlTextAppendChar(out, *url);
        }
        return out->failed ? E_OUTOFMEMORY : S_OK;
    }
    if (flags & URL_ESCAPE_SEGMENT_ONLY) {
        escape_string(out, url, flags);
        return out->failed ? E_OUTOFMEMORY : S_OK;
    }
    SIZE_T length = wcslen(url);
    LPWSTR copy = KbAlloc((length + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, url, (length + 1) * sizeof(WCHAR));
    URL_PARTS parts;
    KbUrlBreak(copy, &parts);
    URL_TEXT store;
    KbUrlTextInit(&store);
    HRESULT hr = S_OK;
    if (parts.count && !escape_segments(&parts, flags, &store))
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        KbUrlBuild(&parts, flags, out);
        if (out->failed)
            hr = E_OUTOFMEMORY;
    }
    KbUrlTextFree(&store);
    KbUrlPartsFree(&parts);
    KbFree(copy);
    return hr;
}

/* ------------------------------------------------------------------ paths and urls */

LPCWSTR KbUrlFindDosPath(LPCWSTR url)
{
    if ((KbIsDriveLetter(url[0]) && url[1] == L':') || PathIsUNCW(url))
        return url;
    SIZE_T length = KbUrlSchemeLength(url);
    if (length <= 1 || KbUrlSchemeType(url, length, nullptr) != URL_SCHEME_FILE)
        return nullptr;
    LPCWSTR p = url + length + 1;
    int slashes = 0;
    while (p[slashes] == L'/' || p[slashes] == L'\\')
        slashes++;
    if (slashes == 4)
        return p + 2;
    if (slashes == 2 && KbIsDriveLetter(p[2]) && p[3] == L':')
        return p + 2;
    return nullptr;
}

static void trim_insignificant(LPWSTR s)
{
    LPWSTR in = s, out = s;
    while (*in && *in <= 0x20)
        in++;
    for (; *in; in++)
        if (*in != L'\t' && *in != L'\n' && *in != L'\r')
            *out++ = *in;
    *out = 0;
    while (out > s && out[-1] <= 0x20)
        *--out = 0;
}

HRESULT KbUrlCreateFromPath(LPCWSTR path, URL_TEXT *out, DWORD flags)
{
    if (PathIsURLW(path)) {
        KbUrlTextSet(out, path);
        return out->failed ? E_OUTOFMEMORY : S_FALSE;
    }
    SIZE_T length = wcslen(path);
    LPWSTR copy = KbAlloc((length + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, path, (length + 1) * sizeof(WCHAR));
    trim_insignificant(copy);
    URL_PARTS parts;
    memset(&parts, 0, sizeof(parts));
    parts.flags = PART_SLASHES;
    parts.scheme = (LPWSTR)L"file";
    parts.type = URL_SCHEME_FILE;
    LPWSTR fragment = (LPWSTR)KbUrlFindFragment(copy, TRUE);
    if (fragment) {
        *fragment = 0;
        parts.fragment = fragment + 1;
    }
    for (LPWSTR p = copy; *p; p++)
        if (*p == L'\\')
            *p = L'/';
    LPWSTR p = copy;
    file_server(&p, &parts);
    if (*p) {
        if (!parts.server && *p == L'/') {
            parts.flags |= PART_ROOTED;
            p++;
        }
        if (*p) {
            if (is_drive(p))
                parts.flags |= PART_DRIVE;
            add_segments(p, &parts);
        }
    }
    HRESULT hr = S_OK;
    URL_TEXT store, escaped_fragment;
    KbUrlTextInit(&store);
    KbUrlTextInit(&escaped_fragment);
    KbUrlTextSet(out, L"");
    if (!(flags & URL_FILE_USE_PATHURL)) {
        DWORD escape_flags = flags | URL_ESCAPE_PERCENT;
        if (parts.count && !escape_segments(&parts, escape_flags, &store))
            hr = E_OUTOFMEMORY;
        if (parts.fragment) {
            escape_string(&escaped_fragment, parts.fragment, escape_flags);
            parts.fragment = (LPWSTR)text_string(&escaped_fragment);
        }
        if (SUCCEEDED(hr))
            KbUrlBuild(&parts, flags, out);
        if (SUCCEEDED(hr) && (flags & URL_WININET_COMPATIBILITY))
            wininet_slashes(out->text);
    } else {
        text_append_string(out, L"file://");
        hr = build_dos_path(&parts, out, flags, TRUE);
    }
    if (SUCCEEDED(hr) && out->failed)
        hr = E_OUTOFMEMORY;
    KbUrlTextFree(&store);
    KbUrlTextFree(&escaped_fragment);
    KbUrlPartsFree(&parts);
    KbFree(copy);
    return hr;
}

HRESULT KbUrlPathFromUrl(LPCWSTR url, URL_TEXT *out, DWORD flags)
{
    KbUrlTextSet(out, L"");
    if (!url || !*url)
        return S_FALSE;
    SIZE_T length = wcslen(url);
    LPWSTR copy = KbAlloc((length + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, url, (length + 1) * sizeof(WCHAR));
    URL_PARTS parts;
    KbUrlBreak(copy, &parts);
    HRESULT hr = E_INVALIDARG;
    if (parts.type == URL_SCHEME_FILE)
        hr = build_dos_path(&parts, out, flags, (flags & URL_FILE_USE_PATHURL) != 0);
    KbUrlPartsFree(&parts);
    KbFree(copy);
    if (SUCCEEDED(hr) && out->failed)
        hr = E_OUTOFMEMORY;
    return hr;
}

/* ------------------------------------------------------------------ the older canonicalization */

static void clear_parts(URL_PARTS *parts)
{
    memset(parts, 0, sizeof(*parts));
}

/* a rooted relative path replaces the base's, below its drive if it has one. otherwise it goes in
   the base's folder, dropping the base's last segment. */
static void blend_path(URL_PARTS *rel, URL_PARTS *base, URL_PARTS *result)
{
    if (rel->flags & PART_ROOTED) {
        if ((base->flags & PART_DRIVE) && !(rel->flags & PART_DRIVE) && base->count) {
            result->flags |= base->flags & 0xF00;
            result->path = base->path;
            result->count = 1;
            result->more = rel->path;
            result->more_count = rel->count;
            result->flags |= rel->flags & 0xF000;
        } else {
            result->path = rel->path;
            result->count = rel->count;
            result->flags |= rel->flags & 0xFF00;
        }
        clear_parts(base);
    } else if (base->flags & PART_ROOTED) {
        result->flags |= base->flags & 0xF00;
        result->path = base->path;
        result->count = base->count;
        if (rel->count || (rel->flags & PART_EMPTY)) {
            result->more = rel->path;
            result->more_count = rel->count;
            result->flags |= rel->flags & 0xF000;
            if (!(base->flags & PART_DIRECTORY)) {
                int last = last_live(base->path, base->count);
                if (last >= 0 && !(last == 0 && (base->flags & PART_DRIVE)) && !is_dot_dot(base->path[last].text)) {
                    if (rel->flags & PART_EMPTY)
                        result->flags |= PART_DIRECTORY;
                    base->path[last].dead = TRUE;
                }
            }
        } else {
            result->flags |= base->flags & 0xF000;
        }
    } else if (rel->count) {
        result->path = rel->path;
        result->count = rel->count;
        result->flags |= rel->flags & 0xFF00;
    } else if (base->count) {
        result->path = base->path;
        result->count = base->count;
        result->flags |= base->flags & 0xFF00;
    }
    if (rel->count)
        clear_parts(base);
}

/* what the relative url lacks comes from the base, until a part differs from the base's: from
   there on the base contributes nothing. */
static void blend(URL_PARTS *rel, URL_PARTS *base, URL_PARTS *result)
{
    memset(result, 0, sizeof(*result));
    if (rel->scheme) {
        result->flags |= rel->flags & 0xFF;
        result->scheme = rel->scheme;
        result->type = rel->type;
        if ((rel->type && rel->type != base->type) || rel->type == URL_SCHEME_FILE || !base->scheme ||
            wcscmp(rel->scheme, base->scheme))
            clear_parts(base);
    } else {
        result->scheme = base->scheme;
        result->type = base->type;
        result->flags |= base->flags & 0xFF;
    }
    if (rel->server) {
        result->server = rel->server;
        if (base->server && wcscmp(rel->server, base->server))
            clear_parts(base);
    } else {
        result->server = base->server;
    }
    blend_path(rel, base, result);
    if (rel->query) {
        result->query = rel->query;
        if (base->query && wcscmp(rel->query, base->query))
            clear_parts(base);
    } else {
        result->query = base->query;
    }
    if (rel->fragment || rel->more_count)
        result->fragment = rel->fragment;
    else
        result->fragment = base->fragment;
}

HRESULT KbUrlParse(LPCWSTR base, LPCWSTR relative, URL_TEXT *out, DWORD flags)
{
    KbUrlTextSet(out, L"");
    if (relative[0] == L'#' && (!base || !*base)) {
        KbUrlTextSet(out, relative);
        return out->failed ? E_OUTOFMEMORY : S_OK;
    }
    URL_TEXT work, base_work;
    KbUrlTextInit(&work);
    KbUrlTextInit(&base_work);
    HRESULT hr = S_OK;
    LPCWSTR dos = nullptr;
    if (flags & URL_WININET_COMPATIBILITY) {
        if (is_drive(relative) || PathIsUNCW(relative)) {
            KbUrlTextSet(&work, L"file://");
            text_append_string(&work, relative);
        } else {
            KbUrlTextSet(&work, relative);
        }
    } else if ((dos = KbUrlFindDosPath(relative))) {
        hr = KbUrlCreateFromPath(dos, &work, flags);
    } else {
        KbUrlTextSet(&work, relative);
    }
    if (FAILED(hr) || work.failed) {
        KbUrlTextFree(&work);
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    LPWSTR url = work.text;
    trim_insignificant(url);
    if (!wcscmp(url, L"://")) {
        KbUrlTextSet(out, L":///");
        KbUrlTextFree(&work);
        return S_OK;
    }

    URL_PARTS rel, base_parts, result;
    KbUrlBreak(url, &rel);
    memset(&base_parts, 0, sizeof(base_parts));
    BOOL absolute = TRUE;
    if (base) {
        absolute = FALSE;
        if (rel.scheme) {
            SIZE_T length = KbUrlSchemeLength(base);
            absolute = length <= 1 || KbUrlSchemeType(base, length, nullptr) != rel.type;
        }
        if (!absolute) {
            LPCWSTR base_dos = (flags & URL_WININET_COMPATIBILITY) ? nullptr : KbUrlFindDosPath(base);
            if (base_dos) {
                hr = KbUrlCreateFromPath(base_dos, &base_work, flags);
            } else if ((flags & URL_WININET_COMPATIBILITY) && (is_drive(base) || PathIsUNCW(base))) {
                KbUrlTextSet(&base_work, L"file://");
                text_append_string(&base_work, base);
            } else {
                KbUrlTextSet(&base_work, base);
            }
            if (SUCCEEDED(hr) && !base_work.failed) {
                trim_insignificant(base_work.text);
                KbUrlBreak(base_work.text, &base_parts);
            }
        }
    }
    URL_PARTS *final = &rel;
    if (!absolute && SUCCEEDED(hr)) {
        URL_PARTS base_copy = base_parts;
        blend(&rel, &base_copy, &result);
        final = &result;
    }
    if (SUCCEEDED(hr)) {
        if (!(flags & URL_DONT_SIMPLIFY))
            canon_parts(final);
        if (final->type == URL_SCHEME_FILE && (flags & URL_FILE_USE_PATHURL)) {
            KbUrlTextSet(out, L"file://");
            hr = build_dos_path(final, out, flags, TRUE);
        } else {
            KbUrlBuild(final, flags, out);
        }
    }
    if (SUCCEEDED(hr) && out->failed)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && (flags & URL_UNESCAPE)) {
        hr = KbUrlUnescapeInPlace(out->text, flags);
        out->length = wcslen(out->text);
    }
    if (SUCCEEDED(hr) && (flags & (URL_ESCAPE_UNSAFE | URL_ESCAPE_SPACES_ONLY))) {
        URL_TEXT escaped;
        KbUrlTextInit(&escaped);
        hr = KbUrlEscape(text_string(out), &escaped, flags);
        if (SUCCEEDED(hr))
            KbUrlTextSet(out, text_string(&escaped));
        KbUrlTextFree(&escaped);
    }
    if (SUCCEEDED(hr) && (flags & URL_WININET_COMPATIBILITY) && final->type == URL_SCHEME_FILE)
        wininet_slashes(out->text);
    KbUrlPartsFree(&rel);
    KbUrlPartsFree(&base_parts);
    KbUrlTextFree(&work);
    KbUrlTextFree(&base_work);
    return hr;
}
