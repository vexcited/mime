/*
 * urls. UrlCanonicalize and UrlCombine try the newer engine first: it streams a base and a relative url into one output
 * that escapes, unescapes and resolves dots as it goes. the older engine lives in urlparts.c.
 */
#include "url_private.h"

/* ------------------------------------------------------------------ schemes and characters */

const URL_SCHEME_INFO KbUrlSchemes[24] = {
    {L"http", URL_SCHEME_HTTP, SCHEME_SERVER | SCHEME_SLASHES},
    {L"file", URL_SCHEME_FILE, SCHEME_SLASHES},
    {L"ftp", URL_SCHEME_FTP, SCHEME_SERVER | SCHEME_SLASHES},
    {L"https", URL_SCHEME_HTTPS, SCHEME_SERVER | SCHEME_SLASHES | SCHEME_SECURE},
    {L"news", URL_SCHEME_NEWS, SCHEME_SERVER | SCHEME_SLASHES},
    {L"mailto", URL_SCHEME_MAILTO, SCHEME_OPAQUE},
    {L"search", URL_SCHEME_SEARCH, SCHEME_SLASHES},
    {L"search-ms", URL_SCHEME_SEARCH_MS, SCHEME_SLASHES},
    {L"gopher", URL_SCHEME_GOPHER, SCHEME_SERVER | SCHEME_SLASHES},
    {L"nntp", URL_SCHEME_NNTP, SCHEME_SERVER | SCHEME_SLASHES},
    {L"telnet", URL_SCHEME_TELNET, SCHEME_SERVER | SCHEME_SLASHES},
    {L"wais", URL_SCHEME_WAIS, 0},
    {L"mk", URL_SCHEME_MK, SCHEME_NOHISTORY},
    {L"shell", URL_SCHEME_SHELL, SCHEME_OPAQUE},
    {L"knownfolder", URL_SCHEME_KNOWNFOLDER, SCHEME_OPAQUE},
    {L"local", URL_SCHEME_LOCAL, 0},
    {L"javascript", URL_SCHEME_JAVASCRIPT, SCHEME_OPAQUE | SCHEME_NOHISTORY},
    {L"vbscript", URL_SCHEME_VBSCRIPT, SCHEME_OPAQUE | SCHEME_NOHISTORY},
    {L"snews", URL_SCHEME_SNEWS, SCHEME_SERVER | SCHEME_SLASHES},
    {L"about", URL_SCHEME_ABOUT, SCHEME_OPAQUE | SCHEME_NOHISTORY},
    {L"res", URL_SCHEME_RES, SCHEME_NOHISTORY},
    {L"ms-shell-rooted", URL_SCHEME_MSSHELLROOTED, 0},
    {L"ms-shell-idlist", URL_SCHEME_MSSHELLIDLIST, 0},
    {L"hcp", URL_SCHEME_MSHELP, 0},
};

URL_SCHEME KbUrlSchemeType(LPCWSTR name, SIZE_T length, DWORD *flags)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(KbUrlSchemes); i++) {
        if (wcslen(KbUrlSchemes[i].name) == length && !_wcsnicmp(name, KbUrlSchemes[i].name, length)) {
            if (flags)
                *flags = KbUrlSchemes[i].flags;
            return KbUrlSchemes[i].type;
        }
    }
    if (flags)
        *flags = 0;
    return URL_SCHEME_UNKNOWN;
}

BOOL KbUrlClass(WCHAR c, int mask)
{
    if (c < 0x20 || c > 0x7F)
        return FALSE;
    int bits = 0;
    if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))
        bits |= URLC_ALNUM;
    if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'))
        bits |= URLC_HEX;
    if (c == L'+' || c == L'-' || c == L'.')
        bits |= URLC_SCHEME;
    if (wcschr(L"!$%'()*+,-.:;=@_~", c))
        bits |= URLC_SAFE;
    if (c == L'-' || c == L'.' || c == L'_' || c == L'~')
        bits |= URLC_UNRESERVED;
    return (bits & mask) != 0;
}

BOOL KbUrlSchemeChar(WCHAR c)
{
    return KbUrlClass(c, URLC_ALNUM | URLC_SCHEME);
}

int KbUrlHexValue(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    return -1;
}

SIZE_T KbUrlSchemeLength(LPCWSTR url)
{
    SIZE_T i = 0;
    for (; url[i] && url[i] != L':'; i++)
        if (!KbUrlSchemeChar(url[i]))
            return 0;
    return url[i] == L':' && i > 1 ? i : 0;
}

static BOOL is_letter(WCHAR c)
{
    return KbIsDriveLetter(c);
}

/* tabs, line feeds and carriage returns are dropped anywhere in a url. */
static BOOL insignificant(WCHAR c)
{
    return c == L'\t' || c == L'\n' || c == L'\r';
}

static LPCWSTR next_char(LPCWSTR p)
{
    while (insignificant(*p))
        p++;
    return p;
}

/* ------------------------------------------------------------------ newer engine output */

static const WCHAR empty_url[] = L"";

typedef struct {
    LPWSTR text;
    DWORD capacity;
    DWORD pos;                  /* the length plus one */
    URL_SCHEME scheme;
    DWORD scheme_flags;
    DWORD mark;                 /* ".." and erasing never go back past this */
    DWORD white;                /* where trailing white space started */
    DWORD query, fragment;      /* positions after "?" and of "#" */
    DWORD original_flags, flags;
    int mode;                   /* 1 unescapes, 2 escapes what is unsafe, 3 escapes spaces */
    BOOL slashes;               /* backslashes written as slashes */
    BOOL percent;               /* inside a percent escape being decoded */
    WCHAR first_hex;
    BOOL failed;
} OUT_URL;

typedef struct {
    LPCWSTR cur;
    URL_SCHEME scheme;
    DWORD flags;
    BOOL simplify;              /* dots are resolved */
    BOOL query_only;            /* a file url whose relative part is a query */
} IN_URL;

static void put(OUT_URL *o, WCHAR c)
{
    if (o->failed)
        return;
    if (o->pos + 1 > o->capacity) {
        DWORD capacity = o->capacity * 2;
        LPWSTR text = KbAlloc((SIZE_T)capacity * sizeof(WCHAR));
        if (!text) {
            o->failed = TRUE;
            return;
        }
        memcpy(text, o->text, (SIZE_T)(o->pos - 1) * sizeof(WCHAR));
        KbFree(o->text);
        o->text = text;
        o->capacity = capacity;
    }
    o->text[o->pos - 1] = c;
    o->pos++;
}

static void base_accept(OUT_URL *o, WCHAR c)
{
    put(o, c == L'\\' && o->slashes ? L'/' : c);
}

static void track_white(OUT_URL *o, WCHAR c)
{
    if (c <= 0x20) {
        if (!o->white)
            o->white = o->pos;
    } else {
        o->white = 0;
    }
}

static void put_escape(OUT_URL *o, WCHAR c)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    put(o, L'%');
    put(o, hex[(c >> 4) & 15]);
    put(o, hex[c & 15]);
}

/* when unescaping, a decoded backslash stays a backslash. escaping leaves slashes alone and escapes
   anything below 0x100 that is neither alphanumeric nor safe. */
static void accept(OUT_URL *o, WCHAR c)
{
    track_white(o, c);
    switch (o->mode) {
    case 2:
        if (c == L'/')
            break;
        if (c == L'\\') {
            if (o->slashes)
                break;
            put_escape(o, c);
            return;
        }
        if (c > 0xFF)
            break;
        if (c < 0x20 || c > 0x7F || !KbUrlClass(c, URLC_ALNUM | URLC_SAFE) ||
            (c == L'%' && (o->flags & URL_ESCAPE_PERCENT))) {
            put_escape(o, c);
            return;
        }
        break;
    case 1:
        if (o->percent) {
            if (KbUrlClass(c, URLC_HEX)) {
                if (!o->first_hex) {
                    o->first_hex = c;
                    return;
                }
                c = (WCHAR)(KbUrlHexValue(o->first_hex) * 16 + KbUrlHexValue(c));
                track_white(o, c);
                o->percent = FALSE;
                if (c == L'\\' && o->slashes) {
                    put(o, L'\\');
                    return;
                }
                break;
            }
            base_accept(o, L'%');
            if (o->first_hex)
                base_accept(o, o->first_hex);
            o->percent = FALSE;
        } else if (c == L'%') {
            o->percent = TRUE;
            o->first_hex = 0;
            return;
        }
        break;
    case 3:
        if (c == L' ') {
            put(o, L'%');
            put(o, L'2');
            put(o, L'0');
            return;
        }
        break;
    }
    base_accept(o, c);
}

/* no munging in an opaque url escaping only spaces, nor in a query or fragment that is kept as is
   or escaped as unsafe. */
static void enable_munging(OUT_URL *o)
{
    o->mode = 0;
    if ((o->scheme_flags & SCHEME_OPAQUE) && (o->flags & URL_ESCAPE_SPACES_ONLY))
        return;
    if ((o->query || o->fragment) && (o->flags & (URL_ESCAPE_UNSAFE | URL_DONT_ESCAPE_EXTRA_INFO)))
        return;
    if (o->flags & URL_UNESCAPE)
        o->mode = 1;
    else if (o->flags & URL_ESCAPE_UNSAFE)
        o->mode = 2;
    else if (o->flags & URL_ESCAPE_SPACES_ONLY)
        o->mode = 3;
}

static void trim_end_white(OUT_URL *o)
{
    if (o->white) {
        o->pos = o->white;
        o->white = 0;
    }
}

static void erase_marked(OUT_URL *o)
{
    if (o->mark) {
        o->pos = o->mark;
        o->mark = 0;
    }
}

static BOOL marked_equals(OUT_URL *o, LPCWSTR s)
{
    if (!o->mark)
        return FALSE;
    DWORD length = o->pos - o->mark;
    return wcslen(s) == length && !wcsncmp(o->text + o->mark - 1, s, length);
}

/* steps back to the previous separator, never past the mark. force also steps back from a trailing one. */
static void contract(OUT_URL *o, BOOL force)
{
    int at = (int)o->pos - 2, floor = (o->mark ? (int)o->mark : 1) - 1;
    BOOL mk = o->scheme == URL_SCHEME_MK;
    if (!force && at >= 0 && (o->text[at] == L'/' || (!mk && o->text[at] == L'\\')))
        return;
    do
        at--;
    while (at >= floor && o->text[at] != L'/' && (mk || o->text[at] != L'\\'));
    if (at < floor)
        o->pos = floor + 1;
    else
        o->pos = (DWORD)at + 2;
}

/* ------------------------------------------------------------------ detecting */

static BOOL local_drive(LPCWSTR p)
{
    p = next_char(p);
    if (!is_letter(*p))
        return FALSE;
    p = next_char(p + 1);
    return *p == L':' || *p == L'|';
}

static BOOL detect_symbols(IN_URL *u, WCHAR a, WCHAR b)
{
    WCHAR c = *next_char(u->cur);
    return c && (c == a || c == b);
}

static BOOL detect_path(IN_URL *u)
{
    WCHAR c = *next_char(u->cur);
    return c && c != L'#' && c != L'?';
}

/* consumes a leading separator. an opaque url counts as absolute without one. */
static BOOL detect_absolute_path(IN_URL *u)
{
    if (u->flags & SCHEME_OPAQUE)
        return TRUE;
    LPCWSTR p = next_char(u->cur);
    if (*p != L'\\' && *p != L'/')
        return FALSE;
    u->cur = next_char(p + 1);
    return TRUE;
}

/* 1 for ".", 2 for "..", when followed by the end, a slash, a query or a fragment. a backslash
   also ends them in mk urls. */
static int detect_dots(IN_URL *u)
{
    LPCWSTR p = next_char(u->cur);
    if (*p != L'.')
        return 0;
    int dots = 1;
    p = next_char(p + 1);
    if (*p == L'.') {
        dots = 2;
        p = next_char(p + 1);
    }
    if (!*p || *p == L'#' || *p == L'/' || *p == L'?')
        return dots;
    if (*p == L'\\' && u->scheme == URL_SCHEME_MK)
        return dots;
    return 0;
}

static BOOL detect_file_server(IN_URL *u)
{
    LPCWSTR p = next_char(u->cur);
    if (is_letter(*p)) {
        LPCWSTR n = next_char(p + 1);
        if (*n == L':' || *n == L'|') {
            u->flags |= PART_UNC4;
            return TRUE;
        }
    }
    return *p == L'\\' || *p == L'/';
}

static BOOL detect_server(IN_URL *u)
{
    LPCWSTR p = next_char(u->cur);
    if (u->scheme == URL_SCHEME_FILE)
        return detect_file_server(u);
    if (u->scheme == URL_SCHEME_MK) {
        if (*p != L'@')
            return FALSE;
        u->cur = next_char(p + 1);
        return TRUE;
    }
    if (*p != L'\\' && *p != L'/')
        return FALSE;
    LPCWSTR n = next_char(p + 1);
    return *n == L'/' || *n == L'\\';
}

/* ------------------------------------------------------------------ feeding */

static LPCWSTR feed_until(LPCWSTR p, OUT_URL *o, WCHAR a, WCHAR b)
{
    for (;;) {
        p = next_char(p);
        if (!*p || *p == a || *p == b)
            return p;
        accept(o, *p++);
    }
}

/* drops the port when it is the scheme's default. */
static LPCWSTR feed_port(IN_URL *u, LPCWSTR p, OUT_URL *o)
{
    o->mark = o->pos;
    for (p = next_char(p); *p && *p != L'#' && *p != L'/' && *p != L'?' && *p != L'\\'; p = next_char(p + 1))
        accept(o, *p);
    LPCWSTR port = nullptr;
    switch (u->scheme) {
    case URL_SCHEME_FTP:
        port = L":21";
        break;
    case URL_SCHEME_HTTP:
        port = L":80";
        break;
    case URL_SCHEME_GOPHER:
        port = L":70";
        break;
    case URL_SCHEME_HTTPS:
        port = L":443";
        break;
    default:
        break;
    }
    if (port && marked_equals(o, port))
        erase_marked(o);
    o->mark = 0;
    return p;
}

/* copies up to two leading separators as is. */
static LPCWSTR feed_slashes(LPCWSTR p, OUT_URL *o)
{
    p = next_char(p);
    if (*p == L'\\' || *p == L'/') {
        accept(o, *p);
        p = next_char(p + 1);
        if (*p == L'\\' || *p == L'/') {
            accept(o, *p);
            p = next_char(p + 1);
        }
    }
    return p;
}

/* writes the server's separator, or "/" before a query or at the end of a hierarchical url. */
static void feed_server_end(IN_URL *u, LPCWSTR p, OUT_URL *o, BOOL slash_at_end)
{
    u->cur = p;
    if (*p) {
        if (*p != L'?' && *p != L'#') {
            accept(o, *p);
            u->cur = next_char(p + 1);
            return;
        }
        accept(o, L'/');
        return;
    }
    trim_end_white(o);
    if (slash_at_end)
        accept(o, L'/');
}

/* lower cases the host but keeps any user name and password as is. */
static void feed_http_server(IN_URL *u, OUT_URL *o)
{
    LPCWSTR p = feed_slashes(u->cur, o);
    enable_munging(o);
    for (;;) {
        o->mark = o->pos;
        LPCWSTR host = p;
        while (*p && *p != L'\\' && *p != L'#' && *p != L'/' && *p != L'?' && *p != L':' && *p != L'@') {
            WCHAR c = *p;
            accept(o, c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c);
            p = next_char(p + 1);
        }
        BOOL user = *p == L'@';
        if (*p == L':') {
            LPCWSTR q = next_char(p + 1);
            while (*q && *q != L'#' && *q != L'/' && *q != L'?' && *q != L'@' && *q != L'\\')
                q = next_char(q + 1);
            user = *q == L'@';
            if (!user)
                p = feed_port(u, p, o);
        }
        if (!user)
            break;
        erase_marked(o);
        for (p = host; *p != L'@'; p = next_char(p + 1))
            accept(o, *p);
        accept(o, L'@');
        p = next_char(p + 1);
    }
    o->mark = 0;
    o->mode = 0;
    feed_server_end(u, p, o, u->scheme && !(u->flags & SCHEME_OPAQUE));
}

static void feed_ftp_server(IN_URL *u, OUT_URL *o)
{
    LPCWSTR p = feed_slashes(u->cur, o);
    enable_munging(o);
    LPCWSTR q = p;
    while (*q && *q != L'#' && *q != L'/' && *q != L'?' && *q != L'@')
        q = next_char(q + 1);
    if (*q == L'@')
        for (; *p != L'@'; p = next_char(p + 1))
            accept(o, *p);
    while (*p && *p != L'/' && *p != L':') {
        if (*p == L'?' || *p == L'#')
            break;
        WCHAR c = *p;
        accept(o, c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c);
        p = next_char(p + 1);
    }
    if (*p == L':')
        p = feed_port(u, p, o);
    o->mode = 0;
    feed_server_end(u, p, o, TRUE);
}

/* a server scheme lower cases its host and drops a default port, any other keeps its text up to the
   first slash. */
static void feed_default_server(IN_URL *u, OUT_URL *o)
{
    if (!(u->flags & SCHEME_SERVER))
        o->slashes = FALSE;
    LPCWSTR p = feed_slashes(u->cur, o);
    if (u->flags & SCHEME_SERVER) {
        enable_munging(o);
        while (*p && *p != L'\\' && *p != L'/' && *p != L':' && *p != L'?' && *p != L'#') {
            WCHAR c = *p;
            accept(o, c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c);
            p = next_char(p + 1);
        }
        if (*p == L':')
            p = feed_port(u, p, o);
        o->mode = 0;
    } else {
        for (; *p && *p != L'/'; p = next_char(p + 1))
            accept(o, *p);
    }
    feed_server_end(u, p, o, u->scheme && !(u->flags & SCHEME_OPAQUE));
}

static void feed_mk_server(IN_URL *u, OUT_URL *o)
{
    enable_munging(o);
    accept(o, L'@');
    LPCWSTR p = feed_until(u->cur, o, L'/', L'/');
    if (*p) {
        u->cur = next_char(p + 1);
    } else {
        u->cur = p;
        trim_end_white(o);
    }
    accept(o, L'/');
}

/* by slash count: a server after two, five or six, none after one, three or seven. */
static void feed_file_server(IN_URL *u, OUT_URL *o)
{
    LPCWSTR p = next_char(u->cur);
    int slashes = 0;
    while (*p == L'/' || *p == L'\\') {
        slashes++;
        p = next_char(p + 1);
    }
    BOOL server = FALSE;
    switch (slashes) {
    case 0:
        if (local_drive(p))
            o->flags |= URL_ESCAPE_UNSAFE | URL_ESCAPE_PERCENT;
        break;
    case 4:
        o->flags |= URL_ESCAPE_UNSAFE | URL_ESCAPE_PERCENT;
        u->flags |= PART_UNC4;
        [[fallthrough]];
    case 2:
        if (local_drive(p))
            o->flags |= URL_ESCAPE_UNSAFE | URL_ESCAPE_PERCENT;
        server = TRUE;
        break;
    case 5:
    case 6:
        server = TRUE;
        break;
    default:
        break;
    }
    if (!server) {
        accept(o, L'/');
        accept(o, L'/');
        accept(o, L'/');
        u->cur = next_char(p);
        return;
    }
    accept(o, L'/');
    accept(o, L'/');
    if (!local_drive(p)) {
        enable_munging(o);
        LPCWSTR end = feed_until(p, o, L'/', L'\\');
        if (*end) {
            p = next_char(end + 1);
        } else {
            trim_end_white(o);
            p = empty_url;
        }
    }
    u->cur = p;
    accept(o, L'/');
}

static void feed_server(IN_URL *u, OUT_URL *o)
{
    switch (u->scheme) {
    case URL_SCHEME_FTP:
        feed_ftp_server(u, o);
        break;
    case URL_SCHEME_HTTP:
    case URL_SCHEME_HTTPS:
        feed_http_server(u, o);
        break;
    case URL_SCHEME_FILE:
        feed_file_server(u, o);
        break;
    case URL_SCHEME_MK:
        feed_mk_server(u, o);
        break;
    default:
        feed_default_server(u, o);
        break;
    }
}

static void feed_local_drive(IN_URL *u, OUT_URL *o)
{
    LPCWSTR p = next_char(u->cur);
    accept(o, *p);
    p = next_char(p + 1);
    accept(o, *p);
    u->cur = next_char(p + 1);
    o->mode = 0;
}

static const PCWSTR fragment_extensions[] = {L".html", L".htm", L".xml",  L".doc",  L".xls",  L".ppt",  L".rtf",
                                             L".dot",  L".xlw", L".pps",  L".xlt",  L".hta",  L".pot",  L".pdf",
                                             L".xps",  L".xbap", L".xlsx", L".pptx", L".docx"};

static BOOL output_ends_with_extension(OUT_URL *o)
{
    DWORD length = o->pos - 1;
    for (ULONG i = 0; i < RTL_NUMBER_OF(fragment_extensions); i++) {
        DWORD n = (DWORD)wcslen(fragment_extensions[i]);
        if (length > n && !_wcsnicmp(o->text + length - n, fragment_extensions[i], n))
            return TRUE;
    }
    return FALSE;
}

/* resolves dots at the start of each segment when simplifying. a file url keeps "?" in its path,
   and "#" too unless it follows a document extension. */
static void feed_path(IN_URL *u, OUT_URL *o, BOOL set_mark)
{
    LPCWSTR p = next_char(u->cur);
    if (set_mark)
        o->mark = o->pos;
    if (u->flags & SCHEME_OPAQUE) {
        for (; *p; p = next_char(p + 1))
            accept(o, *p);
        u->cur = p;
        trim_end_white(o);
        return;
    }
    for (;;) {
        LPCWSTR segment = p;
        if (u->simplify && *p == L'.') {
            int dots = 1;
            LPCWSTR n = next_char(p + 1);
            if (*n == L'.') {
                dots = 2;
                n = next_char(n + 1);
            }
            if (!*n || *n == L'#' || *n == L'?') {
                p = n;
            } else if (*n == L'/' || *n == L'\\') {
                p = next_char(n + 1);
            } else {
                dots = 0;
            }
            if (dots) {
                if (dots == 2)
                    contract(o, TRUE);
                continue;
            }
            p = segment;
        }
        BOOL separator = FALSE, more = TRUE;
        while (!separator) {
            WCHAR c = *p;
            if (!c) {
                more = FALSE;
                break;
            }
            if (c == L'?') {
                if (u->scheme != URL_SCHEME_FILE) {
                    more = FALSE;
                    break;
                }
                if (u->query_only) {
                    p = empty_url;
                    more = FALSE;
                    break;
                }
                base_accept(o, L'?');
                p = next_char(p + 1);
                continue;
            }
            if (c == L'#') {
                if (u->scheme != URL_SCHEME_FILE || output_ends_with_extension(o)) {
                    more = FALSE;
                    break;
                }
                accept(o, c);
                p = next_char(p + 1);
                continue;
            }
            if (c == L'/' || c == L'\\')
                separator = TRUE;
            accept(o, c);
            p = next_char(p + 1);
        }
        if (!more) {
            u->cur = p;
            if (!*p)
                trim_end_white(o);
            return;
        }
    }
}

/* a relative query replaces the base's. */
static void feed_query_fragment(IN_URL *u, OUT_URL *o)
{
    LPCWSTR p = next_char(u->cur);
    if (u->flags & SCHEME_OPAQUE) {
        for (; *p; p = next_char(p + 1))
            accept(o, *p);
        u->cur = p;
        return;
    }
    if (*p == L'?') {
        base_accept(o, L'?');
        if (o->query) {
            o->pos = o->query;
            o->fragment = 0;
        }
        o->query = o->pos;
        enable_munging(o);
        for (p = next_char(p + 1); *p; p = next_char(p + 1)) {
            if (*p == L'#') {
                if (!o->fragment) {
                    o->fragment = o->pos;
                    base_accept(o, L'#');
                }
            } else {
                accept(o, *p);
            }
        }
    } else {
        if (o->fragment) {
            o->pos = o->fragment;
            o->fragment = 0;
        }
        o->fragment = o->pos;
        base_accept(o, L'#');
        enable_munging(o);
        for (p = next_char(p + 1); *p; p = next_char(p + 1)) {
            if (*p == L'?')
                base_accept(o, L'?');
            else
                accept(o, *p);
        }
    }
    u->cur = p;
    trim_end_white(o);
    o->mark = 0;
}

/* a drive or "\\" means file. known means the relative url had a scheme, and the base only
   continues past its own when it is the same. */
static BOOL detect_and_feed_scheme(IN_URL *u, OUT_URL *o, BOOL known)
{
    LPCWSTR p = next_char(u->cur);
    BOOL dos = FALSE;
    if (is_letter(*p)) {
        LPCWSTR n = next_char(p + 1);
        dos = *n == L':' || *n == L'|';
    }
    if (!dos && *p == L'\\' && *next_char(p + 1) == L'\\')
        dos = TRUE;
    if (dos) {
        u->scheme = URL_SCHEME_FILE;
        if (known) {
            if (o->scheme != URL_SCHEME_FILE)
                u->cur = empty_url;
            return TRUE;
        }
        for (LPCWSTR s = L"file:"; *s; s++)
            accept(o, *s);
        u->flags = SCHEME_SLASHES;
        o->flags |= URL_ESCAPE_UNSAFE | URL_ESCAPE_PERCENT;
        o->scheme = URL_SCHEME_FILE;
        o->scheme_flags = SCHEME_SLASHES;
        o->slashes = TRUE;
        return TRUE;
    }
    LPCWSTR end = p;
    while (*end && *end < 0x80 && KbUrlSchemeChar(*end))
        end = next_char(end + 1);
    if (*end != L':')
        return FALSE;
    if (!known) {
        int count = 0;
        for (LPCWSTR s = p; s <= end; s = next_char(s + 1)) {
            WCHAR c = *s;
            accept(o, c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c);
            count++;
        }
        u->cur = next_char(end + 1);
        DWORD flags;
        u->scheme = KbUrlSchemeType(o->text + o->pos - 1 - count, (SIZE_T)count - 1, &flags);
        u->flags = flags;
        o->scheme_flags = flags;
        o->slashes = (flags & SCHEME_SLASHES) != 0;
        o->scheme = u->scheme;
        return TRUE;
    }
    LPCWSTR s = p;
    DWORD at = 0;
    for (; s <= end; s = next_char(s + 1), at++) {
        WCHAR c = *s >= L'A' && *s <= L'Z' ? (WCHAR)(*s + 32) : *s;
        if (at >= o->pos - 1 || c != o->text[at])
            break;
    }
    u->cur = s > end ? next_char(end + 1) : empty_url;
    return TRUE;
}

/* E_FAIL leaves the urls to the older engine. */
static HRESULT blend_urls(IN_URL *base, IN_URL *rel, OUT_URL *o, DWORD flags)
{
    BOOL rel_scheme = FALSE, base_scheme = FALSE, server_from_base = FALSE, server_from_rel = FALSE;
    LPCWSTR p = next_char(rel->cur);
    BOOL dos = FALSE;
    if (is_letter(*p)) {
        LPCWSTR n = next_char(p + 1);
        dos = *n == L':' || *n == L'|';
    }
    if (!dos && *p == L'\\' && *next_char(p + 1) == L'\\')
        dos = TRUE;
    if (dos) {
        rel_scheme = TRUE;
        rel->scheme = URL_SCHEME_FILE;
        for (LPCWSTR s = L"file:"; *s; s++)
            accept(o, *s);
        rel->flags = SCHEME_SLASHES;
        o->flags |= URL_ESCAPE_UNSAFE | URL_ESCAPE_PERCENT;
        o->scheme_flags = SCHEME_SLASHES;
        o->slashes = TRUE;
        o->scheme = URL_SCHEME_FILE;
    } else {
        LPCWSTR end = p;
        while (*end && *end < 0x80 && KbUrlSchemeChar(*end))
            end = next_char(end + 1);
        if (*end == L':') {
            int count = 0;
            for (LPCWSTR s = p; s <= end; s = next_char(s + 1)) {
                WCHAR c = *s;
                accept(o, c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c);
                count++;
            }
            rel->cur = next_char(end + 1);
            DWORD scheme_flags;
            URL_SCHEME type = KbUrlSchemeType(o->text + o->pos - 1 - count, (SIZE_T)count - 1, &scheme_flags);
            rel_scheme = TRUE;
            rel->scheme = type;
            rel->flags = scheme_flags;
            o->scheme_flags = scheme_flags;
            o->slashes = (scheme_flags & SCHEME_SLASHES) != 0;
            o->scheme = type;
        }
    }
    if (rel_scheme && (o->scheme == URL_SCHEME_FILE || (rel->flags & SCHEME_OPAQUE))) {
        base->cur = empty_url;
    } else {
        base_scheme = detect_and_feed_scheme(base, o, rel_scheme);
        if (base_scheme && !rel_scheme) {
            rel->scheme = base->scheme;
            rel->flags = base->flags;
        }
    }
    if ((o->scheme == URL_SCHEME_FILE || (!rel_scheme && !base_scheme)) &&
        (flags & (URL_WININET_COMPATIBILITY | URL_FILE_USE_PATHURL)))
        return E_FAIL;
    if (o->scheme == URL_SCHEME_UNKNOWN) {
        if (!(flags & URL_PLUGGABLE_PROTOCOL))
            return E_FAIL;
        rel->simplify = FALSE;
        LPCWSTR b = next_char(base->cur);
        BOOL hierarchical;
        if (!*b || base->cur == empty_url) {
            hierarchical = detect_symbols(rel, L'/', L'\\');
        } else {
            hierarchical = detect_symbols(base, L'/', L'\\');
            if (!hierarchical) {
                if (!detect_symbols(rel, L'?', L'#'))
                    base->cur = empty_url;
                base->flags |= SCHEME_OPAQUE;
            }
        }
        if (!hierarchical) {
            rel->flags |= SCHEME_OPAQUE;
            o->scheme_flags |= SCHEME_OPAQUE;
            o->slashes = (o->scheme_flags & SCHEME_SLASHES) != 0;
        }
    } else if (o->scheme == URL_SCHEME_FTP) {
        o->slashes = FALSE;
    }
    if (flags & URL_DONT_SIMPLIFY) {
        base->simplify = FALSE;
        rel->simplify = FALSE;
    }
    enable_munging(o);

    if (!(o->scheme_flags & SCHEME_OPAQUE)) {
        if (detect_server(rel)) {
            BOOL use_rel = !detect_server(base);
            if (!use_rel) {
                LPCWSTR second = next_char(next_char(rel->cur) + 1);
                use_rel = *second == L'/' || *second == L'\\';
            }
            if (use_rel) {
                server_from_rel = TRUE;
                feed_server(rel, o);
                base->cur = empty_url;
            }
        }
        if (!server_from_rel && detect_server(base)) {
            server_from_base = TRUE;
            feed_server(base, o);
        }
    }

    if (o->scheme == URL_SCHEME_FILE) {
        p = next_char(rel->cur);
        if (*p == L'?') {
            base->query_only = TRUE;
        } else {
            BOOL slash = FALSE;
            if (rel->flags & SCHEME_OPAQUE) {
                slash = TRUE;
            } else if (*p == L'\\' || *p == L'/') {
                slash = TRUE;
                rel->cur = next_char(p + 1);
            }
            p = next_char(rel->cur);
            if (local_drive(p)) {
                base->cur = empty_url;
                accept(o, *p);
                p = next_char(p + 1);
                accept(o, *p);
                rel->cur = next_char(p + 1);
                o->mode = 0;
                BOOL add = TRUE;
                if (!(rel->flags & SCHEME_OPAQUE)) {
                    LPCWSTR n = next_char(rel->cur);
                    if (*n == L'\\' || *n == L'/')
                        rel->cur = next_char(n + 1);
                    else
                        add = FALSE;
                }
                if (add)
                    accept(o, L'/');
            } else if (local_drive(base->cur)) {
                feed_local_drive(base, o);
                if (slash) {
                    accept(o, L'/');
                    base->cur = empty_url;
                } else if (detect_absolute_path(base)) {
                    accept(o, L'/');
                }
            } else if (slash) {
                if (server_from_rel)
                    accept(o, L'/');
                base->cur = empty_url;
            }
        }
    }

    enable_munging(o);
    BOOL done = FALSE;
    if (server_from_base && detect_absolute_path(rel)) {
        if (!server_from_rel) {
            o->flags = o->original_flags;
            enable_munging(o);
        }
        if (detect_dots(rel))
            rel->simplify = FALSE;
        feed_path(rel, o, TRUE);
        base->cur = empty_url;
        done = TRUE;
    }
    if (!done) {
        LPCWSTR b = next_char(base->cur);
        if (*b && *b != L'#' && *b != L'?') {
            feed_path(base, o, TRUE);
            if (base->flags & SCHEME_OPAQUE) {
                rel->simplify = FALSE;
                feed_path(rel, o, FALSE);
            } else {
                o->flags = o->original_flags;
                enable_munging(o);
                if (detect_path(rel) || (rel_scheme && !detect_symbols(rel, L'?', L'#'))) {
                    if (!detect_path(rel) && rel_scheme) {
                        rel->simplify = FALSE;
                    } else {
                        contract(o, FALSE);
                        if (rel_scheme)
                            rel->simplify = FALSE;
                    }
                    feed_path(rel, o, FALSE);
                    base->cur = empty_url;
                } else {
                    feed_path(rel, o, FALSE);
                }
            }
        } else {
            LPCWSTR r = next_char(rel->cur);
            if (*r && *r != L'#' && *r != L'?') {
                if (!server_from_rel) {
                    o->flags = o->original_flags;
                    enable_munging(o);
                } else if (*r == L'.') {
                    LPCWSTR n = next_char(r + 1);
                    if (*n == L'.')
                        n = next_char(n + 1);
                    if (!*n || *n == L'#' || *n == L'/' || *n == L'?')
                        rel->simplify = FALSE;
                    else if (*n == L'\\' && o->scheme != URL_SCHEME_MK)
                        rel->simplify = FALSE;
                }
                feed_path(rel, o, TRUE);
                base->cur = empty_url;
            }
        }
    }

    o->mark = 0;
    o->slashes = FALSE;
    WCHAR c = *next_char(base->cur);
    if (c == L'#' || c == L'?')
        feed_query_fragment(base, o);
    c = *next_char(rel->cur);
    if (c == L'#' || c == L'?')
        feed_query_fragment(rel, o);
    put(o, 0);
    o->pos--;
    return o->failed ? E_OUTOFMEMORY : S_OK;
}

static LPCWSTR skip_blanks(LPCWSTR s)
{
    while (*s && *s <= 0x20)
        s++;
    return s;
}

/* the newer engine first, the older one when it gives up, sized like FormUrlCombineResult. */
static HRESULT combine_result(LPCWSTR base, LPCWSTR relative, LPWSTR out, DWORD *size, DWORD flags)
{
    if ((flags & (URL_ESCAPE_UNSAFE | URL_ESCAPE_SPACES_ONLY)) == (URL_ESCAPE_UNSAFE | URL_ESCAPE_SPACES_ONLY))
        flags ^= URL_ESCAPE_UNSAFE;
    DWORD blend_flags = flags;
    if (flags & URL_UNESCAPE)
        blend_flags &= ~(URL_ESCAPE_UNSAFE | URL_ESCAPE_SPACES_ONLY);
    IN_URL b = {skip_blanks(base), URL_SCHEME_UNKNOWN, 0, TRUE, FALSE};
    IN_URL r = {skip_blanks(relative), URL_SCHEME_UNKNOWN, 0, TRUE, FALSE};
    OUT_URL o;
    memset(&o, 0, sizeof(o));
    o.capacity = 256;
    o.text = KbAlloc(o.capacity * sizeof(WCHAR));
    if (!o.text)
        return E_OUTOFMEMORY;
    o.pos = 1;
    o.original_flags = o.flags = blend_flags;
    o.slashes = TRUE;
    HRESULT hr = blend_urls(&b, &r, &o, blend_flags);
    if (SUCCEEDED(hr)) {
        o.text[o.pos - 1] = 0;
        if ((flags & (URL_ESCAPE_UNSAFE | URL_ESCAPE_SPACES_ONLY)) && (flags & URL_UNESCAPE)) {
            if (!out || !*size) {
                hr = E_INVALIDARG;
            } else {
                URL_TEXT escaped;
                KbUrlTextInit(&escaped);
                hr = KbUrlEscape(o.text, &escaped, flags & ~URL_UNESCAPE);
                if (SUCCEEDED(hr))
                    hr = KbUrlCopyOut(escaped.text ? escaped.text : L"", out, size);
                KbUrlTextFree(&escaped);
            }
        } else {
            DWORD length = o.pos - 1;
            if (length + 1 > *size) {
                *size = length + 1;
                hr = E_POINTER;
            } else {
                if (out)
                    memcpy(out, o.text, (SIZE_T)(length + 1) * sizeof(WCHAR));
                *size = length;
                hr = S_OK;
            }
        }
    } else if (hr == E_FAIL) {
        URL_TEXT parsed;
        KbUrlTextInit(&parsed);
        hr = KbUrlParse(base, relative, &parsed, flags);
        if (SUCCEEDED(hr))
            hr = KbUrlCopyOut(parsed.text ? parsed.text : L"", out, size);
        KbUrlTextFree(&parsed);
    }
    KbFree(o.text);
    return hr;
}

/* ------------------------------------------------------------------ canonicalizing and combining */

LWSTDAPI HRESULT WINAPI UrlCanonicalizeW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD flags)
{
    if (!url || !out || !size || !*size)
        return E_INVALIDARG;
    return combine_result(L"", url, out, size, flags);
}

LWSTDAPI HRESULT WINAPI UrlCombineW(LPCWSTR base, LPCWSTR relative, LPWSTR out, DWORD *size, DWORD flags)
{
    if (!base || !relative || !size)
        return E_INVALIDARG;
    return combine_result(base, relative, out, size, flags);
}

/* the ansi forms call the wide ones and convert the result with the ansi code page. */
static HRESULT ansi_result(HRESULT hr, LPCWSTR wide, LPSTR out, DWORD *size)
{
    if (FAILED(hr))
        return hr;
    return KbUrlCopyOutA(wide, out, size);
}

static HRESULT combine_ansi(LPCSTR base, LPCSTR relative, LPSTR out, DWORD *size, DWORD flags)
{
    PWSTR b = KbShAnsiToWide(base), r = KbShAnsiToWide(relative);
    HRESULT hr = E_OUTOFMEMORY;
    if (b && r) {
        DWORD wide_size = INTERNET_MAX_URL_LENGTH;
        LPWSTR wide = KbAlloc(wide_size * sizeof(WCHAR));
        if (wide) {
            hr = combine_result(b, r, wide, &wide_size, flags);
            if (hr == E_POINTER) {
                KbFree(wide);
                wide = KbAlloc((SIZE_T)wide_size * sizeof(WCHAR));
                hr = wide ? combine_result(b, r, wide, &wide_size, flags) : E_OUTOFMEMORY;
            }
            if (wide)
                hr = ansi_result(hr, wide, out, size);
            KbFree(wide);
        }
    }
    KbFree(b);
    KbFree(r);
    return hr;
}

LWSTDAPI HRESULT WINAPI UrlCanonicalizeA(LPCSTR url, LPSTR out, DWORD *size, DWORD flags)
{
    if (!url || !out || !size || !*size)
        return E_INVALIDARG;
    return combine_ansi("", url, out, size, flags);
}

LWSTDAPI HRESULT WINAPI UrlCombineA(LPCSTR base, LPCSTR relative, LPSTR out, DWORD *size, DWORD flags)
{
    if (!base || !relative || !size)
        return E_INVALIDARG;
    return combine_ansi(base, relative, out, size, flags);
}

/* ------------------------------------------------------------------ escaping */

HRESULT KbUrlUnescapeInPlace(LPWSTR url, DWORD flags)
{
    LPWSTR in = url, out = url;
    while (*in) {
        WCHAR c = *in;
        if ((c == L'#' || c == L'?') && (flags & URL_DONT_UNESCAPE_EXTRA_INFO)) {
            while (*in)
                *out++ = *in++;
            break;
        }
        if (c == L'%' && KbUrlClass(in[1], URLC_HEX) && KbUrlClass(in[2], URLC_HEX)) {
            if (flags & URL_UNESCAPE_AS_UTF8) {
                char bytes[256];
                int count = 0;
                while (in[0] == L'%' && KbUrlClass(in[1], URLC_HEX) && KbUrlClass(in[2], URLC_HEX) &&
                       count < (int)sizeof(bytes) - 1) {
                    BYTE b = (BYTE)(KbUrlHexValue(in[1]) * 16 + KbUrlHexValue(in[2]));
                    if (!b) {
                        *out = 0;
                        return E_INVALIDARG;
                    }
                    bytes[count++] = (char)b;
                    in += 3;
                }
                bytes[count] = 0;
                int n = MultiByteToWideChar(CP_UTF8, 0, bytes, count + 1, out, count + 1);
                if (n > 0)
                    out += n - 1;
                continue;
            }
            BYTE b = (BYTE)(KbUrlHexValue(in[1]) * 16 + KbUrlHexValue(in[2]));
            if (!b)
                return E_INVALIDARG;
            *out++ = b;
            in += 3;
            continue;
        }
        *out++ = *in++;
    }
    *out = 0;
    return S_OK;
}

/* URL_UNESCAPE_INPLACE works on url itself with no size checks. "%00" is E_INVALIDARG. */
LWSTDAPI HRESULT WINAPI UrlUnescapeW(LPWSTR url, LPWSTR out, DWORD *size, DWORD flags)
{
    if (flags & URL_UNESCAPE_INPLACE)
        return KbUrlUnescapeInPlace(url, flags);
    if (!url || !size || !*size || !out)
        return E_INVALIDARG;
    SIZE_T length = wcslen(url);
    LPWSTR copy = KbAlloc((length + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, url, (length + 1) * sizeof(WCHAR));
    HRESULT hr = KbUrlUnescapeInPlace(copy, flags);
    if (SUCCEEDED(hr))
        hr = KbUrlCopyOut(copy, out, size);
    KbFree(copy);
    return hr;
}

static HRESULT unescape_in_place_a(LPSTR url, DWORD flags)
{
    LPSTR in = url, out = url;
    while (*in) {
        char c = *in;
        if ((c == '#' || c == '?') && (flags & URL_DONT_UNESCAPE_EXTRA_INFO)) {
            while (*in)
                *out++ = *in++;
            break;
        }
        if (c == '%' && KbUrlClass((BYTE)in[1], URLC_HEX) && KbUrlClass((BYTE)in[2], URLC_HEX)) {
            char b = (char)(KbUrlHexValue((BYTE)in[1]) * 16 + KbUrlHexValue((BYTE)in[2]));
            if (!b) {
                *out = 0;
                return E_INVALIDARG;
            }
            *out++ = b;
            in += 3;
            continue;
        }
        *out++ = *in++;
    }
    *out = 0;
    return S_OK;
}

LWSTDAPI HRESULT WINAPI UrlUnescapeA(LPSTR url, LPSTR out, DWORD *size, DWORD flags)
{
    if (flags & URL_UNESCAPE_INPLACE)
        return unescape_in_place_a(url, flags);
    if (!url || !size || !*size || !out)
        return E_INVALIDARG;
    if (flags & URL_UNESCAPE_AS_UTF8)
        return E_INVALIDARG;
    SIZE_T length = strlen(url);
    LPSTR copy = KbAlloc(length + 1);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, url, length + 1);
    unescape_in_place_a(copy, flags);
    length = strlen(copy);
    HRESULT hr;
    if (*size > length) {
        memcpy(out, copy, length + 1);
        *size = (DWORD)length;
        hr = S_OK;
    } else {
        *size = (DWORD)length + 1;
        hr = E_POINTER;
    }
    KbFree(copy);
    return hr;
}

LWSTDAPI HRESULT WINAPI UrlEscapeW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD flags)
{
    if (!url || !out || !size || !*size)
        return E_INVALIDARG;
    URL_TEXT escaped;
    KbUrlTextInit(&escaped);
    HRESULT hr = KbUrlEscape(url, &escaped, flags);
    if (SUCCEEDED(hr))
        hr = KbUrlCopyOut(escaped.text ? escaped.text : L"", out, size);
    KbUrlTextFree(&escaped);
    return hr;
}

LWSTDAPI HRESULT WINAPI UrlEscapeA(LPCSTR url, LPSTR out, DWORD *size, DWORD flags)
{
    if (!url || !out || !size || !*size)
        return E_INVALIDARG;
    if (flags & (URL_ESCAPE_AS_UTF8 | URL_ESCAPE_ASCII_URI_COMPONENT))
        return E_NOTIMPL;
    PWSTR w = KbShAnsiToWide(url);
    if (!w)
        return E_OUTOFMEMORY;
    URL_TEXT escaped;
    KbUrlTextInit(&escaped);
    HRESULT hr = KbUrlEscape(w, &escaped, flags);
    if (SUCCEEDED(hr))
        hr = KbUrlCopyOutA(escaped.text ? escaped.text : L"", out, size);
    KbUrlTextFree(&escaped);
    KbFree(w);
    return hr;
}

/* ------------------------------------------------------------------ parts */

/* splits "user:password@host:port". a bracketed host keeps its colons. */
static void break_logon(LPWSTR server, LPWSTR *user, LPWSTR *password, LPWSTR *host, LPWSTR *port)
{
    *user = *password = *port = nullptr;
    *host = server;
    LPWSTR at = wcschr(server, L'@');
    if (at) {
        *at = 0;
        *user = server;
        *host = at + 1;
        LPWSTR colon = wcschr(server, L':');
        if (colon) {
            *colon = 0;
            *password = colon + 1;
        }
    }
    LPWSTR p = *host;
    if (*p == L'[') {
        LPWSTR close = wcschr(p, L']');
        if (close)
            p = close;
    }
    LPWSTR colon = wcschr(p, L':');
    if (colon) {
        *colon = 0;
        *port = colon + 1;
    }
}

static HRESULT get_part(LPCWSTR url, DWORD part, DWORD flags, URL_TEXT *out)
{
    SIZE_T length = wcslen(url);
    LPWSTR copy = KbAlloc((length + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, url, (length + 1) * sizeof(WCHAR));
    URL_PARTS parts;
    KbUrlBreak(copy, &parts);
    KbUrlTextSet(out, L"");
    if (flags & URL_PARTFLAG_KEEPSCHEME) {
        KbUrlTextSet(out, parts.scheme ? parts.scheme : L"");
        KbUrlTextAppendChar(out, L':');
    }
    HRESULT hr = S_OK;
    switch (part) {
    case URL_PART_SCHEME:
        KbUrlTextSet(out, parts.scheme ? parts.scheme : L"");
        break;
    case URL_PART_QUERY:
        KbUrlTextSet(out, parts.query ? parts.query : L"");
        break;
    case URL_PART_HOSTNAME:
        if (parts.type == URL_SCHEME_FILE) {
            KbUrlTextSet(out, parts.server ? parts.server : L"");
            break;
        }
        [[fallthrough]];
    case URL_PART_USERNAME:
    case URL_PART_PASSWORD:
    case URL_PART_PORT: {
        if (!(parts.flags & PART_SERVER) || !parts.server) {
            hr = E_FAIL;
            break;
        }
        LPWSTR user, password, host, port;
        break_logon(parts.server, &user, &password, &host, &port);
        LPCWSTR piece = part == URL_PART_HOSTNAME ? host
                        : part == URL_PART_USERNAME ? user
                        : part == URL_PART_PASSWORD ? password
                                                    : port;
        if (piece)
            KbUrlTextAppend(out, piece, wcslen(piece));
        break;
    }
    default:
        hr = E_UNEXPECTED;
        break;
    }
    KbUrlPartsFree(&parts);
    KbFree(copy);
    if (SUCCEEDED(hr) && out->failed)
        hr = E_OUTOFMEMORY;
    return hr;
}

LWSTDAPI HRESULT WINAPI UrlGetPartW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD part, DWORD flags)
{
    if (!url || !out || !size || !*size || !part)
        return E_INVALIDARG;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = get_part(url, part, flags, &text);
    if (SUCCEEDED(hr))
        hr = KbUrlCopyOut(text.text ? text.text : L"", out, size);
    KbUrlTextFree(&text);
    return hr;
}

LWSTDAPI HRESULT WINAPI UrlGetPartA(LPCSTR url, LPSTR out, DWORD *size, DWORD part, DWORD flags)
{
    if (!url || !out || !size || !*size || !part)
        return E_INVALIDARG;
    PWSTR w = KbShAnsiToWide(url);
    if (!w)
        return E_OUTOFMEMORY;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = get_part(w, part, flags, &text);
    if (SUCCEEDED(hr))
        hr = KbUrlCopyOutA(text.text ? text.text : L"", out, size);
    KbUrlTextFree(&text);
    KbFree(w);
    return hr;
}

/* ------------------------------------------------------------------ what a url is */

/* guesses a scheme for "www.", "ftp.", "mosaic" and "home" urls that lack one. */
static BOOL guess_scheme(LPCWSTR url, URL_TEXT *out)
{
    static const struct {
        PCWSTR prefix;
        PCWSTR scheme;
    } guesses[] = {{L"www", L"http"}, {L"ftp", L"ftp"}, {L"mosaic", L"http"}, {L"home", L"http"}};
    for (ULONG i = 0; i < RTL_NUMBER_OF(guesses); i++) {
        if (url[0] == L'/' && url[1] == L'/')
            url += 2;
        SIZE_T length = wcslen(guesses[i].prefix);
        if (!_wcsnicmp(url, guesses[i].prefix, length) && url[length]) {
            KbUrlTextSet(out, guesses[i].scheme);
            KbUrlTextAppend(out, L"://", 3);
            KbUrlTextAppend(out, url, wcslen(url));
            return TRUE;
        }
    }
    return FALSE;
}

LWSTDAPI BOOL WINAPI UrlIsW(LPCWSTR url, URLIS what)
{
    if (!url)
        return FALSE;
    SIZE_T length = KbUrlSchemeLength(url);
    if (!length)
        return FALSE;
    DWORD flags;
    URL_SCHEME type = KbUrlSchemeType(url, length, &flags);
    switch (what) {
    case URLIS_URL:
        return TRUE;
    case URLIS_OPAQUE:
        return flags & SCHEME_OPAQUE;
    case URLIS_NOHISTORY:
        return flags & SCHEME_NOHISTORY;
    case URLIS_FILEURL:
        return type == URL_SCHEME_FILE;
    case URLIS_APPLIABLE: {
        if (type)
            return FALSE;
        URL_TEXT guessed;
        KbUrlTextInit(&guessed);
        BOOL result = guess_scheme(url, &guessed);
        KbUrlTextFree(&guessed);
        return result;
    }
    case URLIS_DIRECTORY:
    case URLIS_HASQUERY: {
        SIZE_T n = wcslen(url);
        LPWSTR copy = KbAlloc((n + 1) * sizeof(WCHAR));
        if (!copy)
            return FALSE;
        memcpy(copy, url, (n + 1) * sizeof(WCHAR));
        URL_PARTS parts;
        KbUrlBreak(copy, &parts);
        BOOL result = what == URLIS_DIRECTORY ? (!parts.count || (parts.flags & PART_DIRECTORY))
                                              : (parts.query && *parts.query);
        KbUrlPartsFree(&parts);
        KbFree(copy);
        return result;
    }
    }
    return FALSE;
}

LWSTDAPI BOOL WINAPI UrlIsA(LPCSTR url, URLIS what)
{
    if (!url)
        return FALSE;
    PWSTR w = KbShAnsiToWide(url);
    BOOL result = w && UrlIsW(w, what);
    KbFree(w);
    return result;
}

LWSTDAPI BOOL WINAPI UrlIsOpaqueW(LPCWSTR url)
{
    return UrlIsW(url, URLIS_OPAQUE);
}

LWSTDAPI BOOL WINAPI UrlIsOpaqueA(LPCSTR url)
{
    return UrlIsA(url, URLIS_OPAQUE);
}

LWSTDAPI BOOL WINAPI UrlIsNoHistoryW(LPCWSTR url)
{
    return UrlIsW(url, URLIS_NOHISTORY);
}

LWSTDAPI BOOL WINAPI UrlIsNoHistoryA(LPCSTR url)
{
    return UrlIsA(url, URLIS_NOHISTORY);
}

LWSTDAPI BOOL WINAPI PathIsURLW(LPCWSTR path)
{
    return path && KbUrlSchemeLength(path) != 0;
}

LWSTDAPI BOOL WINAPI PathIsURLA(LPCSTR path)
{
    if (!path)
        return FALSE;
    PWSTR w = KbShAnsiToWide(path);
    BOOL result = w && PathIsURLW(w);
    KbFree(w);
    return result;
}

/* a file url's suffix skips "//" and a third slash. */
LWSTDAPI HRESULT WINAPI ParseURLW(LPCWSTR url, PARSEDURLW *parsed)
{
    if (!url || !parsed || parsed->cbSize != sizeof(PARSEDURLW))
        return E_INVALIDARG;
    SIZE_T length = KbUrlSchemeLength(url);
    if (!length) {
        parsed->pszProtocol = nullptr;
        return URL_E_INVALID_SYNTAX;
    }
    parsed->pszProtocol = url;
    parsed->cchProtocol = (UINT)length;
    parsed->nScheme = KbUrlSchemeType(url, length, nullptr);
    LPCWSTR suffix = url + length + 1;
    if (parsed->nScheme == URL_SCHEME_FILE && suffix[0] == L'/' && suffix[1] == L'/') {
        suffix += 2;
        if (*suffix == L'/')
            suffix++;
    }
    parsed->pszSuffix = suffix;
    parsed->cchSuffix = (UINT)wcslen(suffix);
    return S_OK;
}

LWSTDAPI HRESULT WINAPI ParseURLA(LPCSTR url, PARSEDURLA *parsed)
{
    if (!url || !parsed || parsed->cbSize != sizeof(PARSEDURLA))
        return E_INVALIDARG;
    UINT i = 0;
    for (; url[i] && url[i] != ':'; i++)
        if (!KbUrlSchemeChar((BYTE)url[i]))
            break;
    if (url[i] != ':' || i <= 1) {
        parsed->pszProtocol = nullptr;
        return URL_E_INVALID_SYNTAX;
    }
    WCHAR scheme[64];
    UINT n = i < 63 ? i : 63;
    for (UINT k = 0; k < n; k++)
        scheme[k] = (BYTE)url[k];
    parsed->pszProtocol = url;
    parsed->cchProtocol = i;
    parsed->nScheme = n == i ? KbUrlSchemeType(scheme, n, nullptr) : URL_SCHEME_UNKNOWN;
    LPCSTR suffix = url + i + 1;
    if (parsed->nScheme == URL_SCHEME_FILE && suffix[0] == '/' && suffix[1] == '/') {
        suffix += 2;
        if (*suffix == '/')
            suffix++;
    }
    parsed->pszSuffix = suffix;
    parsed->cchSuffix = (UINT)strlen(suffix);
    return S_OK;
}

/* in a file url, "#" only counts after a document extension. */
LWSTDAPI LPCWSTR WINAPI UrlGetLocationW(LPCWSTR url)
{
    if (!url)
        return nullptr;
    SIZE_T length = KbUrlSchemeLength(url);
    if (!length)
        return nullptr;
    DWORD flags;
    URL_SCHEME type = KbUrlSchemeType(url, length, &flags);
    if (flags & SCHEME_OPAQUE)
        return nullptr;
    return KbUrlFindFragment(url, type == URL_SCHEME_FILE);
}

LWSTDAPI LPCSTR WINAPI UrlGetLocationA(LPCSTR url)
{
    if (!url)
        return nullptr;
    PWSTR w = KbShAnsiToWide(url);
    if (!w)
        return nullptr;
    LPCWSTR found = UrlGetLocationW(w);
    LPCSTR result = nullptr;
    if (found) {
        int bytes = WideCharToMultiByte(CP_ACP, 0, w, (int)(found - w), nullptr, 0, nullptr, nullptr);
        result = url + bytes;
    }
    KbFree(w);
    return result;
}

/* ------------------------------------------------------------------ from paths and back */

LWSTDAPI HRESULT WINAPI UrlCreateFromPathW(LPCWSTR path, LPWSTR url, DWORD *size, DWORD flags)
{
    if (!path || !url || !size || !*size)
        return E_INVALIDARG;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = KbUrlCreateFromPath(path, &text, flags);
    if (SUCCEEDED(hr)) {
        HRESULT copy = KbUrlCopyOut(text.text ? text.text : L"", url, size);
        if (FAILED(copy))
            hr = copy;
    }
    KbUrlTextFree(&text);
    return hr;
}

LWSTDAPI HRESULT WINAPI UrlCreateFromPathA(LPCSTR path, LPSTR url, DWORD *size, DWORD flags)
{
    if (!path || !url || !size || !*size)
        return E_INVALIDARG;
    PWSTR w = KbShAnsiToWide(path);
    if (!w)
        return E_OUTOFMEMORY;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = KbUrlCreateFromPath(w, &text, flags);
    if (SUCCEEDED(hr)) {
        HRESULT copy = KbUrlCopyOutA(text.text ? text.text : L"", url, size);
        if (FAILED(copy))
            hr = copy;
    }
    KbUrlTextFree(&text);
    KbFree(w);
    return hr;
}

LWSTDAPI HRESULT WINAPI PathCreateFromUrlW(LPCWSTR url, LPWSTR path, DWORD *size, DWORD flags)
{
    if (!url || !path || !size || !*size)
        return E_INVALIDARG;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = KbUrlPathFromUrl(url, &text, flags);
    if (SUCCEEDED(hr))
        hr = KbUrlCopyOut(text.text ? text.text : L"", path, size);
    KbUrlTextFree(&text);
    return hr;
}

LWSTDAPI HRESULT WINAPI PathCreateFromUrlA(LPCSTR url, LPSTR path, DWORD *size, DWORD flags)
{
    if (!url || !path || !size || !*size)
        return E_INVALIDARG;
    PWSTR w = KbShAnsiToWide(url);
    if (!w)
        return E_OUTOFMEMORY;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = KbUrlPathFromUrl(w, &text, flags);
    if (SUCCEEDED(hr))
        hr = KbUrlCopyOutA(text.text ? text.text : L"", path, size);
    KbUrlTextFree(&text);
    KbFree(w);
    return hr;
}

/* the caller frees the path with LocalFree. */
LWSTDAPI HRESULT WINAPI PathCreateFromUrlAlloc(LPCWSTR url, LPWSTR *path, DWORD flags)
{
    if (!url || !path) {
        if (path)
            *path = nullptr;
        return E_INVALIDARG;
    }
    *path = nullptr;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = KbUrlPathFromUrl(url, &text, flags);
    if (SUCCEEDED(hr)) {
        *path = StrDupW(text.text ? text.text : L"");
        hr = *path ? S_OK : E_OUTOFMEMORY;
    }
    KbUrlTextFree(&text);
    return hr;
}

/* ------------------------------------------------------------------ applying schemes */

/* S_FALSE when the url keeps its scheme or none applies. */
static HRESULT apply_scheme(LPCWSTR url, URL_TEXT *out, DWORD flags)
{
    if (!(flags & URL_APPLY_FORCEAPPLY) && KbUrlSchemeLength(url))
        return S_FALSE;
    if ((flags & URL_APPLY_GUESSSCHEME) && guess_scheme(url, out))
        return S_OK;
    if (flags & URL_APPLY_GUESSFILE) {
        LPCWSTR dos = KbUrlFindDosPath(url);
        if (dos && SUCCEEDED(KbUrlCreateFromPath(dos, out, 0)))
            return S_OK;
    }
    if ((flags & URL_APPLY_DEFAULT) || !flags) {
        if (url[0] == L'/' && url[1] == L'/')
            url += 2;
        KbUrlTextSet(out, L"http://");
        KbUrlTextAppend(out, url, wcslen(url));
        return out->failed ? E_OUTOFMEMORY : S_OK;
    }
    return S_FALSE;
}

LWSTDAPI HRESULT WINAPI UrlApplySchemeW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD flags)
{
    if (!url || !out || !size || !*size)
        return E_INVALIDARG;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = apply_scheme(url, &text, flags);
    if (hr == S_OK)
        hr = KbUrlCopyOut(text.text ? text.text : L"", out, size);
    KbUrlTextFree(&text);
    return hr;
}

LWSTDAPI HRESULT WINAPI UrlApplySchemeA(LPCSTR url, LPSTR out, DWORD *size, DWORD flags)
{
    if (!url || !out || !size || !*size)
        return E_INVALIDARG;
    PWSTR w = KbShAnsiToWide(url);
    if (!w)
        return E_OUTOFMEMORY;
    URL_TEXT text;
    KbUrlTextInit(&text);
    HRESULT hr = apply_scheme(w, &text, flags);
    if (hr == S_OK)
        hr = KbUrlCopyOutA(text.text ? text.text : L"", out, size);
    KbUrlTextFree(&text);
    KbFree(w);
    return hr;
}

/* Levenshtein distance. */
static SIZE_T edit_distance(LPCWSTR a, SIZE_T a_length, LPCWSTR b)
{
    SIZE_T b_length = wcslen(b), row[40];
    if (b_length >= RTL_NUMBER_OF(row))
        return (SIZE_T)-1;
    for (SIZE_T j = 0; j <= b_length; j++)
        row[j] = j;
    for (SIZE_T i = 1; i <= a_length; i++) {
        SIZE_T diagonal = row[0];
        row[0] = i;
        for (SIZE_T j = 1; j <= b_length; j++) {
            SIZE_T above = row[j];
            SIZE_T cost = diagonal + (a[i - 1] != b[j - 1]);
            if (above + 1 < cost)
                cost = above + 1;
            if (row[j - 1] + 1 < cost)
                cost = row[j - 1] + 1;
            row[j] = cost;
            diagonal = above;
        }
    }
    return row[b_length];
}

/* keeps a known or registered scheme, else picks the closest known one, adding "//" for server
   schemes. S_FALSE when nothing is close enough. */
LWSTDAPI HRESULT WINAPI UrlFixupW(LPCWSTR url, LPWSTR out, DWORD size)
{
    if (!url || !out)
        return E_INVALIDARG;
    SIZE_T length = 0;
    while (url[length] && url[length] != L':' && url[length] != L';') {
        if (!KbUrlSchemeChar(url[length]))
            return S_FALSE;
        length++;
    }
    if (!url[length] || length <= 1 || length > 31)
        return S_FALSE;
    WCHAR scheme[32];
    for (SIZE_T i = 0; i < length; i++)
        scheme[i] = url[i] >= L'A' && url[i] <= L'Z' ? (WCHAR)(url[i] + 32) : url[i];
    scheme[length] = 0;
    SIZE_T rest_length = wcslen(url + length + 1);
    LPWSTR rest = KbAlloc((rest_length + 1) * sizeof(WCHAR));
    if (!rest)
        return E_OUTOFMEMORY;
    memcpy(rest, url + length + 1, (rest_length + 1) * sizeof(WCHAR));
    LPCWSTR fixed = nullptr;
    for (ULONG i = 0; i < RTL_NUMBER_OF(KbUrlSchemes) && !fixed; i++)
        if (!wcscmp(scheme, KbUrlSchemes[i].name))
            fixed = KbUrlSchemes[i].name;
    if (!fixed) {
        HKEY key;
        if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, scheme, 0, KEY_QUERY_VALUE, &key)) {
            if (!RegQueryValueExW(key, L"URL Protocol", nullptr, nullptr, nullptr, nullptr))
                fixed = scheme;
            RegCloseKey(key);
        }
    }
    if (!fixed) {
        /* allow one edit for up to four letters, two beyond. ties go to the earlier table entry. */
        SIZE_T best = length <= 4 ? 2 : 3;
        for (ULONG i = 0; i < RTL_NUMBER_OF(KbUrlSchemes); i++) {
            if (KbUrlSchemes[i].flags & SCHEME_SECURE)
                continue;
            SIZE_T distance = edit_distance(scheme, length, KbUrlSchemes[i].name);
            if (distance < best) {
                best = distance;
                fixed = KbUrlSchemes[i].name;
            }
        }
    }
    HRESULT hr = S_FALSE;
    if (fixed && size > wcslen(fixed) + 3) {
        LPCWSTR tail = rest;
        BOOL slashes = !wcscmp(fixed, L"http") || !wcscmp(fixed, L"https") || !wcscmp(fixed, L"ftp");
        WCHAR buffer[INTERNET_MAX_URL_LENGTH];
        SIZE_T n = wcslen(fixed);
        memcpy(buffer, fixed, n * sizeof(WCHAR));
        buffer[n++] = L':';
        if (slashes) {
            int skipped = 0;
            while (*tail && skipped < 3 && !KbUrlClass(*tail, URLC_ALNUM)) {
                tail++;
                skipped++;
            }
            buffer[n++] = L'/';
            buffer[n++] = L'/';
        }
        buffer[n] = 0;
        KbShCopy(out, size, buffer);
        SIZE_T used = wcslen(out);
        if (used < size)
            KbShCopy(out + used, size - used, tail);
        hr = S_OK;
    }
    KbFree(rest);
    return hr;
}

/* ------------------------------------------------------------------ hashes, comparisons, languages */

static const BYTE hash_table[256] = {
#include "urlhash.h"
};

static void hash_bytes(const BYTE *data, DWORD size, BYTE *hash, DWORD hash_size)
{
    for (DWORD i = hash_size; i; i--)
        hash[i - 1] = (BYTE)(i - 1);
    while (size--)
        for (DWORD i = hash_size; i; i--)
            hash[i - 1] = hash_table[hash[i - 1] ^ data[size]];
}

LWSTDAPI HRESULT WINAPI HashData(const BYTE *data, DWORD size, BYTE *hash, DWORD hash_size)
{
    if (!data || !hash)
        return E_INVALIDARG;
    hash_bytes(data, size, hash, hash_size);
    return S_OK;
}

LWSTDAPI HRESULT WINAPI UrlHashA(LPCSTR url, BYTE *hash, DWORD size)
{
    if (!url || !hash)
        return E_INVALIDARG;
    hash_bytes((const BYTE *)url, (DWORD)strlen(url), hash, size);
    return S_OK;
}

LWSTDAPI HRESULT WINAPI UrlHashW(LPCWSTR url, BYTE *hash, DWORD size)
{
    if (!url || !hash)
        return E_INVALIDARG;
    int bytes = WideCharToMultiByte(CP_ACP, 0, url, -1, nullptr, 0, nullptr, nullptr);
    LPSTR ansi = bytes > 0 ? KbAlloc((SIZE_T)bytes) : nullptr;
    if (!ansi)
        return E_OUTOFMEMORY;
    WideCharToMultiByte(CP_ACP, 0, url, -1, ansi, bytes, nullptr, nullptr);
    HRESULT hr = UrlHashA(ansi, hash, size);
    KbFree(ansi);
    return hr;
}

static void drop_final_slash(LPWSTR s)
{
    SIZE_T length = wcslen(s);
    if (length && s[length - 1] == L'/')
        s[length - 1] = 0;
}

/* unescapes both, optionally drops a final slash, then compares as strings. */
LWSTDAPI int WINAPI UrlCompareW(LPCWSTR url1, LPCWSTR url2, BOOL ignore_slash)
{
    if (!url1 || !url2)
        return StrCmpW(url1, url2);
    LPWSTR a = StrDupW(url1), b = StrDupW(url2);
    int result;
    if (!a || !b || FAILED(KbUrlUnescapeInPlace(a, 0)) || FAILED(KbUrlUnescapeInPlace(b, 0))) {
        result = StrCmpW(url1, url2);
    } else {
        if (ignore_slash) {
            drop_final_slash(a);
            drop_final_slash(b);
        }
        result = StrCmpW(a, b);
    }
    LocalFree(a);
    LocalFree(b);
    return result;
}

LWSTDAPI int WINAPI UrlCompareA(LPCSTR url1, LPCSTR url2, BOOL ignore_slash)
{
    if (!url1 || !url2)
        return lstrcmpA(url1, url2);
    PWSTR a = KbShAnsiToWide(url1), b = KbShAnsiToWide(url2);
    int result = a && b ? UrlCompareW(a, b, ignore_slash) : lstrcmpA(url1, url2);
    KbFree(a);
    KbFree(b);
    return result;
}

/* internet explorer's AcceptLanguage setting, else the user's locale name. */
LWSTDAPI HRESULT WINAPI GetAcceptLanguagesW(LPWSTR languages, DWORD *size)
{
    if (!size)
        return E_INVALIDARG;
    if (languages && *size)
        *languages = 0;
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Internet Explorer\\International", 0,
                               KEY_QUERY_VALUE, &key);
    if (!error) {
        DWORD bytes = *size * sizeof(WCHAR);
        error = RegGetValueW(key, nullptr, L"AcceptLanguage", RRF_RT_REG_SZ, nullptr, languages, &bytes);
        RegCloseKey(key);
        if (!error) {
            *size = bytes / sizeof(WCHAR) - 1;
            return languages && !*languages ? S_FALSE : S_OK;
        }
    }
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        return HRESULT_FROM_WIN32(error);
    if (!languages) {
        *size = LOCALE_NAME_MAX_LENGTH;
        return HRESULT_FROM_WIN32(error);
    }
    int n = GetUserDefaultLocaleName(languages, (int)*size);
    if (!n) {
        *size = 0;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *size = (DWORD)n - 1;
    return S_OK;
}

LWSTDAPI HRESULT WINAPI GetAcceptLanguagesA(LPSTR languages, DWORD *size)
{
    if (!size)
        return E_INVALIDARG;
    DWORD wide_size = languages ? *size : 0;
    LPWSTR wide = languages ? KbAlloc((SIZE_T)(*size ? *size : 1) * sizeof(WCHAR)) : nullptr;
    if (languages && !wide)
        return E_OUTOFMEMORY;
    HRESULT hr = GetAcceptLanguagesW(wide, &wide_size);
    if (SUCCEEDED(hr) && wide) {
        if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, languages, (int)*size, nullptr, nullptr)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
        } else {
            languages[*size - 1] = 0;
            *size = (DWORD)strlen(languages);
        }
    } else if (SUCCEEDED(hr) || !wide) {
        *size = wide_size;
    }
    KbFree(wide);
    return hr;
}

/* internet explorer enhanced security, off unless the user set IEHarden. */
LWSTDAPI BOOL WINAPI IsInternetESCEnabled(void)
{
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\ZoneMap",
                     L"IEHarden", RRF_RT_REG_DWORD, nullptr, &value, &size))
        return FALSE;
    return value == 1;
}
