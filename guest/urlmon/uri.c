/*
 * IUri: CreateUri splits a uri per rfc 3986 and canonicalizes known schemes like windows does: scheme and host
 * lowered, dot segments removed, escaped unreserved characters decoded, forbidden ones escaped, the root path added
 * and default ports left out of the authority.
 */
#include "urlmon_private.h"

enum { KIND_UNKNOWN, KIND_HIERARCHICAL, KIND_FILE, KIND_OPAQUE, KIND_MAILTO, KIND_WILDCARD };

typedef struct {
    LPCWSTR Name;
    DWORD Scheme;
    DWORD Port;
    DWORD Kind;
} SCHEME_INFO;

/* the known schemes and their default ports. */
static const SCHEME_INFO Schemes[] = {
    {L"ftp", URL_SCHEME_FTP, 21, KIND_HIERARCHICAL},
    {L"http", URL_SCHEME_HTTP, 80, KIND_HIERARCHICAL},
    {L"gopher", URL_SCHEME_GOPHER, 70, KIND_HIERARCHICAL},
    {L"mailto", URL_SCHEME_MAILTO, 0, KIND_MAILTO},
    {L"news", URL_SCHEME_NEWS, 0, KIND_HIERARCHICAL},
    {L"nntp", URL_SCHEME_NNTP, 119, KIND_HIERARCHICAL},
    {L"telnet", URL_SCHEME_TELNET, 23, KIND_HIERARCHICAL},
    {L"wais", URL_SCHEME_WAIS, 210, KIND_HIERARCHICAL},
    {L"file", URL_SCHEME_FILE, 0, KIND_FILE},
    {L"mk", URL_SCHEME_MK, 0, KIND_OPAQUE},
    {L"https", URL_SCHEME_HTTPS, 443, KIND_HIERARCHICAL},
    {L"shell", URL_SCHEME_SHELL, 0, KIND_OPAQUE},
    {L"snews", URL_SCHEME_SNEWS, 0, KIND_HIERARCHICAL},
    {L"local", URL_SCHEME_LOCAL, 0, KIND_HIERARCHICAL},
    {L"javascript", URL_SCHEME_JAVASCRIPT, 0, KIND_OPAQUE},
    {L"vbscript", URL_SCHEME_VBSCRIPT, 0, KIND_OPAQUE},
    {L"about", URL_SCHEME_ABOUT, 0, KIND_OPAQUE},
    {L"res", URL_SCHEME_RES, 0, KIND_HIERARCHICAL},
    {L"ms-shell-rooted", URL_SCHEME_MSSHELLROOTED, 0, KIND_HIERARCHICAL},
    {L"ms-shell-idlist", URL_SCHEME_MSSHELLIDLIST, 0, KIND_HIERARCHICAL},
    {L"hcp", URL_SCHEME_MSHELP, 0, KIND_HIERARCHICAL},
    {L"*", URL_SCHEME_WILDCARD, 0, KIND_WILDCARD},
};

static const SCHEME_INFO UnknownScheme = {nullptr, URL_SCHEME_UNKNOWN, 0, KIND_UNKNOWN};

typedef struct {
    IUri Interface;
    LONG volatile Refs;
    DWORD Flags;
    const SCHEME_INFO *Info;
    PWSTR Raw;
    PWSTR SchemeName;
    BOOL HasAuthority;
    BOOL HasUserInfo;
    BOOL HasPassword;
    PWSTR UserName;
    PWSTR Password;
    PWSTR Host;
    DWORD HostType;
    BOOL HasPort;
    BOOL PortExplicit;
    DWORD Port;
    PWSTR Path;
    PWSTR Query;
    PWSTR Fragment;
} URI;

/* ------------------------------------------------------------------ strings */

typedef struct {
    PWSTR Data;
    DWORD Length;
    DWORD Capacity;
    BOOL Failed;
} TEXT;

static void Add(TEXT *text, LPCWSTR data, DWORD length)
{
    if (text->Failed)
        return;
    if (text->Length + length + 1 > text->Capacity) {
        DWORD capacity = (text->Length + length + 1) * 2;
        PWSTR grown = UrlRealloc(text->Data, capacity * sizeof(WCHAR));
        if (!grown) {
            text->Failed = TRUE;
            return;
        }
        text->Data = grown;
        text->Capacity = capacity;
    }
    memcpy(text->Data + text->Length, data, length * sizeof(WCHAR));
    text->Length += length;
    text->Data[text->Length] = 0;
}

static void AddString(TEXT *text, LPCWSTR data)
{
    if (data)
        Add(text, data, (DWORD)lstrlenW(data));
}

static void AddChar(TEXT *text, WCHAR c)
{
    Add(text, &c, 1);
}

/* NULL when building failed. an empty builder gives an empty string. */
static PWSTR Finish(TEXT *text)
{
    if (text->Failed) {
        UrlFree(text->Data);
        return nullptr;
    }
    if (!text->Data)
        return UrlDuplicate(L"");
    return text->Data;
}

static BOOL IsAlpha(WCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static BOOL IsDigit(WCHAR c)
{
    return c >= '0' && c <= '9';
}

static int HexValue(WCHAR c)
{
    if (IsDigit(c))
        return c - '0';
    if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
        return (c | 0x20) - 'a' + 10;
    return -1;
}

static void Lower(PWSTR text)
{
    for (; text && *text; text++)
        if (*text >= 'A' && *text <= 'Z')
            *text = (WCHAR)(*text + 32);
}

static BOOL IsUnreserved(WCHAR c)
{
    return IsAlpha(c) || IsDigit(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

/* the characters a canonical uri carries escaped. */
static BOOL IsForbidden(WCHAR c)
{
    return c < 0x20 || c == 0x7F || c == ' ' || c == '"' || c == '<' || c == '>' || c == '`' || c == '{' ||
           c == '}' || c == '|' || c == '^' || c == '\\';
}

/* decodes escaped unreserved characters, escapes forbidden ones and stray percent signs with upper case digits. */
static PWSTR CanonicalPart(LPCWSTR part, BOOL decode, BOOL encode)
{
    TEXT text = {0};
    for (LPCWSTR p = part; *p; p++) {
        if (*p == '%') {
            int high = HexValue(p[1]), low = high >= 0 ? HexValue(p[2]) : -1;
            if (low >= 0) {
                WCHAR value = (WCHAR)(high * 16 + low);
                if (decode && IsUnreserved(value)) {
                    AddChar(&text, value);
                } else {
                    Add(&text, p, 3);
                }
                p += 2;
                continue;
            }
            if (encode) {
                AddString(&text, L"%25");
                continue;
            }
        }
        if (encode && IsForbidden(*p)) {
            WCHAR escaped[4];
            wsprintfW(escaped, L"%%%02X", *p);
            AddString(&text, escaped);
            continue;
        }
        AddChar(&text, *p);
    }
    return Finish(&text);
}

/* rfc 3986 5.2.4, in place. */
static void RemoveDots(PWSTR path)
{
    PWSTR copy = UrlDuplicate(path);
    if (!copy)
        return;
    LPCWSTR in = copy;
    PWSTR out = path;
    while (*in) {
        if (!wcsncmp(in, L"../", 3)) {
            in += 3;
        } else if (!wcsncmp(in, L"./", 2) || !wcsncmp(in, L"/./", 3)) {
            in += 2;
        } else if (!lstrcmpW(in, L"/.")) {
            *out++ = '/';
            break;
        } else if (!wcsncmp(in, L"/../", 4) || !lstrcmpW(in, L"/..")) {
            BOOL last = !in[3];
            in += 3;
            while (out > path && *--out != '/')
                ;
            if (last) {
                *out++ = '/';
                break;
            }
        } else if (!lstrcmpW(in, L".") || !lstrcmpW(in, L"..")) {
            break;
        } else {
            do
                *out++ = *in++;
            while (*in && *in != '/');
        }
    }
    *out = 0;
    UrlFree(copy);
}

/* ------------------------------------------------------------------ parsing */

static const SCHEME_INFO *SchemeInfo(LPCWSTR name, DWORD length)
{
    for (DWORD i = 0; i < ARRAYSIZE(Schemes); i++)
        if ((DWORD)lstrlenW(Schemes[i].Name) == length && !_wcsnicmp(Schemes[i].Name, name, length))
            return &Schemes[i];
    return &UnknownScheme;
}

static BOOL IsIPv4(LPCWSTR host)
{
    DWORD dots = 0, digits = 0;
    for (LPCWSTR p = host; *p; p++) {
        if (*p == '.') {
            if (!digits)
                return FALSE;
            dots++;
            digits = 0;
        } else if (IsDigit(*p)) {
            if (++digits > 3)
                return FALSE;
        } else {
            return FALSE;
        }
    }
    return dots == 3 && digits;
}

/* the authority between "//" and the path: user info, host and port. */
static HRESULT ParseAuthority(URI *uri, LPCWSTR start, LPCWSTR end)
{
    LPCWSTR at = nullptr;
    for (LPCWSTR p = start; p < end; p++)
        if (*p == '@')
            at = p;
    LPCWSTR host = start;
    if (at) {
        uri->HasUserInfo = TRUE;
        LPCWSTR colon = start;
        while (colon < at && *colon != ':')
            colon++;
        uri->UserName = UrlDuplicateN(start, (DWORD)(colon - start));
        if (colon < at) {
            uri->HasPassword = TRUE;
            uri->Password = UrlDuplicateN(colon + 1, (DWORD)(at - colon - 1));
        }
        host = at + 1;
    }
    LPCWSTR host_end = end, port = nullptr;
    if (*host == '[') {
        LPCWSTR close = host;
        while (close < end && *close != ']')
            close++;
        if (close == end)
            return E_INVALIDARG;
        uri->Host = UrlDuplicateN(host + 1, (DWORD)(close - host - 1));
        uri->HostType = Uri_HOST_IPV6;
        if (close + 1 < end) {
            if (close[1] != ':')
                return E_INVALIDARG;
            port = close + 2;
        }
    } else {
        for (LPCWSTR p = host; p < end; p++)
            if (*p == ':') {
                host_end = p;
                port = p + 1;
                break;
            }
        uri->Host = UrlDuplicateN(host, (DWORD)(host_end - host));
    }
    if (!uri->Host)
        return E_OUTOFMEMORY;
    if (port && port < end) {
        DWORD value = 0;
        for (LPCWSTR p = port; p < end; p++) {
            if (!IsDigit(*p))
                return E_INVALIDARG;
            value = value * 10 + (DWORD)(*p - '0');
            if (value > 65535)
                return E_INVALIDARG;
        }
        uri->Port = value;
        uri->HasPort = uri->PortExplicit = TRUE;
    }
    if (uri->HostType != Uri_HOST_IPV6)
        uri->HostType = !uri->Host[0] ? Uri_HOST_UNKNOWN : IsIPv4(uri->Host) ? Uri_HOST_IPV4 : Uri_HOST_DNS;
    return S_OK;
}

/* html uri pre-processing: strips surrounding blanks, tabs and line breaks. */
static PWSTR PreProcess(LPCWSTR text, DWORD flags)
{
    DWORD length = (DWORD)lstrlenW(text);
    if (!(flags & Uri_CREATE_NO_PRE_PROCESS_HTML_URI)) {
        while (length && *text <= ' ') {
            text++;
            length--;
        }
        while (length && text[length - 1] <= ' ')
            length--;
    }
    TEXT out = {0};
    for (DWORD i = 0; i < length; i++)
        if (flags & Uri_CREATE_NO_PRE_PROCESS_HTML_URI || (text[i] != '\t' && text[i] != '\r' && text[i] != '\n'))
            AddChar(&out, text[i]);
    return Finish(&out);
}

/* turns a local path into the file url windows makes of it. */
static PWSTR ImplicitFile(LPCWSTR text)
{
    TEXT out = {0};
    BOOL unc = text[0] == '\\' && text[1] == '\\';
    AddString(&out, unc ? L"file:" : L"file:///");
    for (LPCWSTR p = text; *p; p++)
        AddChar(&out, *p == '\\' ? '/' : *p);
    return Finish(&out);
}

static BOOL IsDriveLetter(LPCWSTR text)
{
    return IsAlpha(text[0]) && (text[1] == ':' || text[1] == '|') &&
           (!text[2] || text[2] == '\\' || text[2] == '/');
}

static HRESULT Parse(URI *uri, LPCWSTR input)
{
    DWORD flags = uri->Flags;
    PWSTR text = PreProcess(input, flags);
    if (!text)
        return E_OUTOFMEMORY;
    if (!*text) {
        UrlFree(text);
        return E_INVALIDARG;
    }
    if (flags & Uri_CREATE_ALLOW_IMPLICIT_FILE_SCHEME &&
        (IsDriveLetter(text) || (text[0] == '\\' && text[1] == '\\'))) {
        PWSTR file = ImplicitFile(text);
        UrlFree(text);
        if (!file)
            return E_OUTOFMEMORY;
        text = file;
    }
    LPCWSTR p = text;
    LPCWSTR scheme_end = nullptr;
    if (IsAlpha(*p) || *p == '*') {
        LPCWSTR q = p + 1;
        if (*p != '*')
            while (IsAlpha(*q) || IsDigit(*q) || *q == '+' || *q == '-' || *q == '.')
                q++;
        if (*q == ':')
            scheme_end = q;
    }
    /* a single letter before a colon is a drive, which needs the implicit file flag. */
    if (scheme_end && scheme_end - p == 1 && *p != '*' && IsDriveLetter(p)) {
        UrlFree(text);
        return E_INVALIDARG;
    }
    HRESULT hr = S_OK;
    if (scheme_end) {
        uri->SchemeName = UrlDuplicateN(p, (DWORD)(scheme_end - p));
        Lower(uri->SchemeName);
        uri->Info = SchemeInfo(p, (DWORD)(scheme_end - p));
        p = scheme_end + 1;
    } else if (p[0] == '/' && p[1] == '/' && flags & Uri_CREATE_ALLOW_IMPLICIT_WILDCARD_SCHEME) {
        uri->SchemeName = UrlDuplicate(L"*");
        uri->Info = SchemeInfo(L"*", 1);
    } else if (flags & Uri_CREATE_ALLOW_RELATIVE) {
        uri->Info = &UnknownScheme;
    } else {
        UrlFree(text);
        return E_INVALIDARG;
    }
    DWORD kind = uri->Info->Kind;
    BOOL known = kind == KIND_HIERARCHICAL || kind == KIND_FILE;
    BOOL slash = (p[0] == '/' && p[1] == '/') || (known && p[0] == '\\' && p[1] == '\\');
    BOOL crack = kind == KIND_UNKNOWN && uri->SchemeName && !(flags & Uri_CREATE_NO_CRACK_UNKNOWN_SCHEMES);
    if (kind == KIND_OPAQUE) {
        uri->Path = UrlDuplicate(p);
        p += lstrlenW(p);
    } else if (slash && (known || kind == KIND_WILDCARD || crack || !uri->SchemeName)) {
        LPCWSTR start = p + 2, end = start;
        while (*end && *end != '/' && *end != '?' && *end != '#' && !(known && *end == '\\'))
            end++;
        uri->HasAuthority = TRUE;
        /* a drive letter where the host goes is the path of a file url. */
        if (kind == KIND_FILE && end - start == 2 && IsDriveLetter(start)) {
            uri->Host = UrlDuplicate(L"");
            end = start;
        } else {
            hr = ParseAuthority(uri, start, end);
        }
        p = end;
    } else if (kind == KIND_HIERARCHICAL && uri->Info->Port) {
        /* schemes with servers require one. */
        hr = E_INVALIDARG;
    }
    if (SUCCEEDED(hr) && kind == KIND_HIERARCHICAL && uri->HasAuthority && (!uri->Host || !uri->Host[0]) &&
        uri->Info->Port)
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr) && !uri->Path) {
        LPCWSTR path_end = p;
        while (*path_end && *path_end != '?' && !(*path_end == '#' && !(flags & Uri_CREATE_NOFRAG)))
            path_end++;
        uri->Path = UrlDuplicateN(p, (DWORD)(path_end - p));
        p = path_end;
        if (*p == '?') {
            LPCWSTR query_end = p;
            while (*query_end && !(*query_end == '#' && !(flags & Uri_CREATE_NOFRAG)))
                query_end++;
            uri->Query = UrlDuplicateN(p, (DWORD)(query_end - p));
            p = query_end;
        }
        if (*p == '#')
            uri->Fragment = UrlDuplicate(p);
    }
    BOOL lost = (scheme_end && !uri->SchemeName) || (uri->HasUserInfo && !uri->UserName) ||
                (uri->HasPassword && !uri->Password) || (*p == '?' && !uri->Query) || !uri->Path;
    UrlFree(text);
    if (FAILED(hr))
        return hr;
    return lost ? E_OUTOFMEMORY : S_OK;
}

static HRESULT Canonicalize(URI *uri)
{
    DWORD kind = uri->Info->Kind;
    BOOL canonical = !(uri->Flags & Uri_CREATE_NO_CANONICALIZE);
    BOOL known = kind == KIND_HIERARCHICAL || kind == KIND_FILE || kind == KIND_WILDCARD;
    BOOL encode = canonical && !(uri->Flags & Uri_CREATE_NO_ENCODE_FORBIDDEN_CHARACTERS) && kind != KIND_OPAQUE;
    BOOL decode = canonical && (known || kind == KIND_MAILTO);
    if (known && uri->Host)
        Lower(uri->Host);
    if (kind == KIND_OPAQUE)
        return S_OK;
    PWSTR path = uri->Path;
    if (canonical && known)
        for (PWSTR q = path; *q; q++)
            if (*q == '\\')
                *q = '/';
    PWSTR fixed = CanonicalPart(path, decode, encode);
    if (!fixed)
        return E_OUTOFMEMORY;
    UrlFree(uri->Path);
    uri->Path = fixed;
    if (canonical && known) {
        if (kind == KIND_FILE && IsDriveLetter(uri->Path)) {
            TEXT text = {0};
            AddChar(&text, '/');
            AddString(&text, uri->Path);
            PWSTR rooted = Finish(&text);
            if (!rooted)
                return E_OUTOFMEMORY;
            UrlFree(uri->Path);
            uri->Path = rooted;
            uri->HasAuthority = TRUE;
            if (!uri->Host)
                uri->Host = UrlDuplicate(L"");
        }
        RemoveDots(uri->Path);
        if (uri->HasAuthority && !uri->Path[0]) {
            UrlFree(uri->Path);
            uri->Path = UrlDuplicate(L"/");
            if (!uri->Path)
                return E_OUTOFMEMORY;
        }
    }
    BOOL decode_extra = decode && !(uri->Flags & Uri_CREATE_NO_DECODE_EXTRA_INFO);
    PWSTR *extras[] = {&uri->Query, &uri->Fragment};
    for (int i = 0; i < 2; i++) {
        if (!*extras[i])
            continue;
        PWSTR part = CanonicalPart(*extras[i], decode_extra, encode);
        if (!part)
            return E_OUTOFMEMORY;
        UrlFree(*extras[i]);
        *extras[i] = part;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ properties */

static BOOL DefaultPort(URI *uri)
{
    return uri->Info->Port && uri->Port == uri->Info->Port;
}

static void AddAuthority(TEXT *text, URI *uri, BOOL user_info)
{
    if (user_info && uri->HasUserInfo) {
        AddString(text, uri->UserName);
        if (uri->HasPassword) {
            AddChar(text, ':');
            AddString(text, uri->Password);
        }
        AddChar(text, '@');
    }
    if (uri->HostType == Uri_HOST_IPV6) {
        AddChar(text, '[');
        AddString(text, uri->Host);
        AddChar(text, ']');
    } else {
        AddString(text, uri->Host);
    }
    if (uri->PortExplicit && !(DefaultPort(uri) && !(uri->Flags & Uri_CREATE_NO_CANONICALIZE))) {
        WCHAR port[8];
        wsprintfW(port, L":%u", uri->Port);
        AddString(text, port);
    }
}

static PWSTR BuildUri(URI *uri, BOOL user_info, BOOL fragment)
{
    TEXT text = {0};
    if (uri->SchemeName) {
        AddString(&text, uri->SchemeName);
        AddChar(&text, ':');
    }
    if (uri->HasAuthority) {
        AddString(&text, L"//");
        AddAuthority(&text, uri, user_info);
    }
    AddString(&text, uri->Path);
    AddString(&text, uri->Query);
    if (fragment)
        AddString(&text, uri->Fragment);
    return Finish(&text);
}

/* the registrable domain of a dns host: its last two labels, three under a country's second level. */
static LPCWSTR Domain(URI *uri)
{
    if (uri->HostType != Uri_HOST_DNS)
        return nullptr;
    LPCWSTR host = uri->Host;
    LPCWSTR dots[3] = {nullptr, nullptr, nullptr};
    DWORD count = 0;
    for (LPCWSTR p = host + lstrlenW(host); p > host; p--)
        if (p[-1] == '.' && count < 3)
            dots[count++] = p - 1;
    if (!count)
        return nullptr;
    if (count == 1)
        return host;
    LPCWSTR tld = dots[0] + 1, second = dots[1] + 1;
    DWORD second_length = (DWORD)(dots[0] - second);
    static const LPCWSTR generic[] = {L"co", L"com", L"net", L"org", L"gov", L"edu", L"ac", L"or", L"ne", L"go",
                                      L"mil"};
    BOOL country = lstrlenW(tld) == 2;
    for (DWORD i = 0; country && i < ARRAYSIZE(generic); i++)
        if ((DWORD)lstrlenW(generic[i]) == second_length && !_wcsnicmp(second, generic[i], second_length))
            return count >= 3 ? dots[2] + 1 : host;
    return second;
}

static LPCWSTR Extension(URI *uri)
{
    LPCWSTR path = uri->Path, slash = wcsrchr(path, '/');
    LPCWSTR segment = slash ? slash + 1 : path;
    LPCWSTR dot = wcsrchr(segment, '.');
    return dot ? dot : nullptr;
}

/* returns a heap string, or NULL when the uri has none. */
static PWSTR StringProperty(URI *uri, Uri_PROPERTY property, DWORD flags)
{
    TEXT text = {0};
    switch (property) {
    case Uri_PROPERTY_ABSOLUTE_URI:
        return uri->SchemeName ? BuildUri(uri, TRUE, TRUE) : nullptr;
    case Uri_PROPERTY_AUTHORITY:
        if (!uri->HasAuthority)
            return nullptr;
        AddAuthority(&text, uri, TRUE);
        return Finish(&text);
    case Uri_PROPERTY_DISPLAY_URI:
        return BuildUri(uri, FALSE, !(flags & Uri_DISPLAY_NO_FRAGMENT));
    case Uri_PROPERTY_DOMAIN:
        return UrlDuplicate(Domain(uri));
    case Uri_PROPERTY_EXTENSION:
        return UrlDuplicate(Extension(uri));
    case Uri_PROPERTY_FRAGMENT:
        return UrlDuplicate(uri->Fragment);
    case Uri_PROPERTY_HOST:
        return UrlDuplicate(uri->Host);
    case Uri_PROPERTY_PASSWORD:
        return UrlDuplicate(uri->Password);
    case Uri_PROPERTY_PATH:
        return UrlDuplicate(uri->Path);
    case Uri_PROPERTY_PATH_AND_QUERY:
        AddString(&text, uri->Path);
        AddString(&text, uri->Query);
        return Finish(&text);
    case Uri_PROPERTY_QUERY:
        return UrlDuplicate(uri->Query);
    case Uri_PROPERTY_RAW_URI:
        return UrlDuplicate(uri->Raw);
    case Uri_PROPERTY_SCHEME_NAME:
        return UrlDuplicate(uri->SchemeName);
    case Uri_PROPERTY_USER_INFO:
        if (!uri->HasUserInfo)
            return nullptr;
        AddString(&text, uri->UserName);
        if (uri->HasPassword) {
            AddChar(&text, ':');
            AddString(&text, uri->Password);
        }
        return Finish(&text);
    case Uri_PROPERTY_USER_NAME:
        return UrlDuplicate(uri->UserName);
    default:
        return nullptr;
    }
}

static BOOL HasString(URI *uri, Uri_PROPERTY property)
{
    if (property == Uri_PROPERTY_RAW_URI || property == Uri_PROPERTY_DISPLAY_URI)
        return TRUE;
    if (property == Uri_PROPERTY_ABSOLUTE_URI || property == Uri_PROPERTY_SCHEME_NAME)
        return uri->SchemeName != nullptr;
    PWSTR value = StringProperty(uri, property, 0);
    BOOL has = value && *value;
    UrlFree(value);
    return has;
}

static BOOL ValidFlags(Uri_PROPERTY property, DWORD flags)
{
    if (!flags)
        return TRUE;
    DWORD allowed = 0;
    if (property == Uri_PROPERTY_DISPLAY_URI)
        allowed = Uri_DISPLAY_NO_FRAGMENT | Uri_PUNYCODE_IDN_HOST | Uri_DISPLAY_IDN_HOST;
    else if (property == Uri_PROPERTY_ABSOLUTE_URI || property == Uri_PROPERTY_DOMAIN || property == Uri_PROPERTY_HOST)
        allowed = Uri_PUNYCODE_IDN_HOST | Uri_DISPLAY_IDN_HOST;
    return !(flags & ~allowed);
}

/* ------------------------------------------------------------------ IUri */

static URI *uri_of(IUri *iface)
{
    return (URI *)iface;
}

static HRESULT STDMETHODCALLTYPE u_QueryInterface(IUri *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IUri)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE u_AddRef(IUri *iface)
{
    return (ULONG)InterlockedIncrement(&uri_of(iface)->Refs);
}

static void FreeUri(URI *uri)
{
    UrlFree(uri->Raw);
    UrlFree(uri->SchemeName);
    UrlFree(uri->UserName);
    UrlFree(uri->Password);
    UrlFree(uri->Host);
    UrlFree(uri->Path);
    UrlFree(uri->Query);
    UrlFree(uri->Fragment);
    UrlFree(uri);
}

static ULONG STDMETHODCALLTYPE u_Release(IUri *iface)
{
    URI *uri = uri_of(iface);
    LONG refs = InterlockedDecrement(&uri->Refs);
    if (!refs)
        FreeUri(uri);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE u_GetPropertyBSTR(IUri *iface, Uri_PROPERTY property, BSTR *value, DWORD flags)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (property > Uri_PROPERTY_STRING_LAST)
        return E_INVALIDARG;
    if (!ValidFlags(property, flags))
        return E_INVALIDARG;
    URI *uri = uri_of(iface);
    PWSTR text = StringProperty(uri, property, flags);
    BOOL has = HasString(uri, property);
    *value = SysAllocString(text && has ? text : L"");
    UrlFree(text);
    if (!*value)
        return E_OUTOFMEMORY;
    return has ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE u_GetPropertyLength(IUri *iface, Uri_PROPERTY property, DWORD *length, DWORD flags)
{
    if (!length)
        return E_POINTER;
    *length = 0;
    if (property > Uri_PROPERTY_STRING_LAST || !ValidFlags(property, flags))
        return E_INVALIDARG;
    URI *uri = uri_of(iface);
    if (!HasString(uri, property))
        return S_FALSE;
    PWSTR text = StringProperty(uri, property, flags);
    if (!text)
        return E_OUTOFMEMORY;
    *length = (DWORD)lstrlenW(text);
    UrlFree(text);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE u_GetPropertyDWORD(IUri *iface, Uri_PROPERTY property, DWORD *value, DWORD flags)
{
    if (!value)
        return E_POINTER;
    *value = 0;
    if (property < Uri_PROPERTY_DWORD_START || property > Uri_PROPERTY_DWORD_LAST || flags)
        return E_INVALIDARG;
    URI *uri = uri_of(iface);
    switch (property) {
    case Uri_PROPERTY_HOST_TYPE:
        *value = uri->HostType;
        return S_OK;
    case Uri_PROPERTY_PORT:
        if (!uri->HasPort)
            return S_FALSE;
        *value = uri->Port;
        return S_OK;
    case Uri_PROPERTY_SCHEME:
        *value = uri->SchemeName ? uri->Info->Scheme : URL_SCHEME_UNKNOWN;
        return S_OK;
    default: {
        PWSTR url = BuildUri(uri, TRUE, TRUE);
        *value = url ? UrlZoneOf(url) : (DWORD)-1;
        UrlFree(url);
        return S_OK;
    }
    }
}

static HRESULT STDMETHODCALLTYPE u_HasProperty(IUri *iface, Uri_PROPERTY property, BOOL *has)
{
    if (!has)
        return E_POINTER;
    URI *uri = uri_of(iface);
    if (property <= Uri_PROPERTY_STRING_LAST)
        *has = HasString(uri, property);
    else if (property == Uri_PROPERTY_PORT)
        *has = uri->HasPort;
    else if (property <= Uri_PROPERTY_DWORD_LAST)
        *has = TRUE;
    else
        *has = FALSE;
    return S_OK;
}

#define STRING_GETTER(name, property)                                                                                \
    static HRESULT STDMETHODCALLTYPE u_##name(IUri *iface, BSTR *value)                                              \
    {                                                                                                                 \
        return u_GetPropertyBSTR(iface, property, value, 0);                                                         \
    }

STRING_GETTER(GetAbsoluteUri, Uri_PROPERTY_ABSOLUTE_URI)
STRING_GETTER(GetAuthority, Uri_PROPERTY_AUTHORITY)
STRING_GETTER(GetDisplayUri, Uri_PROPERTY_DISPLAY_URI)
STRING_GETTER(GetDomain, Uri_PROPERTY_DOMAIN)
STRING_GETTER(GetExtension, Uri_PROPERTY_EXTENSION)
STRING_GETTER(GetFragment, Uri_PROPERTY_FRAGMENT)
STRING_GETTER(GetHost, Uri_PROPERTY_HOST)
STRING_GETTER(GetPassword, Uri_PROPERTY_PASSWORD)
STRING_GETTER(GetPath, Uri_PROPERTY_PATH)
STRING_GETTER(GetPathAndQuery, Uri_PROPERTY_PATH_AND_QUERY)
STRING_GETTER(GetQuery, Uri_PROPERTY_QUERY)
STRING_GETTER(GetRawUri, Uri_PROPERTY_RAW_URI)
STRING_GETTER(GetSchemeName, Uri_PROPERTY_SCHEME_NAME)
STRING_GETTER(GetUserInfo, Uri_PROPERTY_USER_INFO)
STRING_GETTER(GetUserName, Uri_PROPERTY_USER_NAME)

static HRESULT STDMETHODCALLTYPE u_GetHostType(IUri *iface, DWORD *value)
{
    return u_GetPropertyDWORD(iface, Uri_PROPERTY_HOST_TYPE, value, 0);
}

static HRESULT STDMETHODCALLTYPE u_GetPort(IUri *iface, DWORD *value)
{
    return u_GetPropertyDWORD(iface, Uri_PROPERTY_PORT, value, 0);
}

static HRESULT STDMETHODCALLTYPE u_GetScheme(IUri *iface, DWORD *value)
{
    return u_GetPropertyDWORD(iface, Uri_PROPERTY_SCHEME, value, 0);
}

static HRESULT STDMETHODCALLTYPE u_GetZone(IUri *iface, DWORD *value)
{
    return u_GetPropertyDWORD(iface, Uri_PROPERTY_ZONE, value, 0);
}

static HRESULT STDMETHODCALLTYPE u_GetProperties(IUri *iface, LPDWORD flags)
{
    if (!flags)
        return E_INVALIDARG;
    DWORD mask = 0;
    for (DWORD property = 0; property <= Uri_PROPERTY_DWORD_LAST; property++) {
        BOOL has;
        u_HasProperty(iface, (Uri_PROPERTY)property, &has);
        if (has)
            mask |= 1u << property;
    }
    *flags = mask;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE u_IsEqual(IUri *iface, IUri *other, BOOL *equal)
{
    if (!equal)
        return E_POINTER;
    *equal = FALSE;
    if (!other)
        return E_POINTER;
    if (other == iface) {
        *equal = TRUE;
        return S_OK;
    }
    BSTR mine = nullptr, theirs = nullptr;
    HRESULT hr = u_GetPropertyBSTR(iface, Uri_PROPERTY_DISPLAY_URI, &mine, 0);
    if (SUCCEEDED(hr))
        hr = other->lpVtbl->GetPropertyBSTR(other, Uri_PROPERTY_DISPLAY_URI, &theirs, 0);
    if (SUCCEEDED(hr)) {
        *equal = !lstrcmpW(mine, theirs);
        if (*equal) {
            SysFreeString(mine);
            SysFreeString(theirs);
            mine = theirs = nullptr;
            u_GetPropertyBSTR(iface, Uri_PROPERTY_USER_INFO, &mine, 0);
            other->lpVtbl->GetPropertyBSTR(other, Uri_PROPERTY_USER_INFO, &theirs, 0);
            *equal = mine && theirs && !lstrcmpW(mine, theirs);
        }
        hr = S_OK;
    }
    SysFreeString(mine);
    SysFreeString(theirs);
    return hr;
}

static const IUriVtbl UriVtbl = {
    u_QueryInterface, u_AddRef,          u_Release,       u_GetPropertyBSTR, u_GetPropertyLength, u_GetPropertyDWORD,
    u_HasProperty,    u_GetAbsoluteUri,  u_GetAuthority,  u_GetDisplayUri,   u_GetDomain,         u_GetExtension,
    u_GetFragment,    u_GetHost,         u_GetPassword,   u_GetPath,         u_GetPathAndQuery,   u_GetQuery,
    u_GetRawUri,      u_GetSchemeName,   u_GetUserInfo,   u_GetUserName,     u_GetHostType,       u_GetPort,
    u_GetScheme,      u_GetZone,         u_GetProperties, u_IsEqual,
};

/* ------------------------------------------------------------------ creation */

/* rejects contradicting flag pairs and fills in the defaults when neither is given. */
static HRESULT CheckFlags(DWORD *flags)
{
    static const DWORD pairs[][2] = {
        {Uri_CREATE_CANONICALIZE, Uri_CREATE_NO_CANONICALIZE},
        {Uri_CREATE_DECODE_EXTRA_INFO, Uri_CREATE_NO_DECODE_EXTRA_INFO},
        {Uri_CREATE_CRACK_UNKNOWN_SCHEMES, Uri_CREATE_NO_CRACK_UNKNOWN_SCHEMES},
        {Uri_CREATE_PRE_PROCESS_HTML_URI, Uri_CREATE_NO_PRE_PROCESS_HTML_URI},
        {Uri_CREATE_IE_SETTINGS, Uri_CREATE_NO_IE_SETTINGS},
    };
    if (*flags & 0xFC0000)
        return E_INVALIDARG;
    for (DWORD i = 0; i < ARRAYSIZE(pairs); i++) {
        if ((*flags & pairs[i][0]) && (*flags & pairs[i][1]))
            return E_INVALIDARG;
        if (!(*flags & (pairs[i][0] | pairs[i][1])))
            *flags |= i == 4 ? pairs[i][1] : pairs[i][0];
    }
    return S_OK;
}

static HRESULT Create(LPCWSTR text, DWORD flags, IUri **result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (!text)
        return E_INVALIDARG;
    HRESULT hr = CheckFlags(&flags);
    if (FAILED(hr))
        return hr;
    URI *uri = UrlAllocZero(sizeof(*uri));
    if (!uri)
        return E_OUTOFMEMORY;
    uri->Interface.lpVtbl = &UriVtbl;
    uri->Refs = 1;
    uri->Flags = flags;
    uri->Info = &UnknownScheme;
    uri->Raw = UrlDuplicate(text);
    hr = uri->Raw ? Parse(uri, text) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
        hr = Canonicalize(uri);
    if (SUCCEEDED(hr) && !uri->PortExplicit && uri->Info->Port &&
        (uri->Info->Kind == KIND_HIERARCHICAL || uri->Info->Kind == KIND_FILE)) {
        uri->HasPort = TRUE;
        uri->Port = uri->Info->Port;
    }
    if (FAILED(hr)) {
        FreeUri(uri);
        return hr;
    }
    *result = &uri->Interface;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI CreateUri(LPCWSTR uri, DWORD flags, DWORD_PTR reserved, IUri **result)
{
    return Create(uri, flags, result);
}

/* a builder of uris is not made yet. */
DECLSPEC_EXPORT HRESULT WINAPI CreateIUriBuilder(IUri *uri, DWORD flags, DWORD_PTR reserved, void **builder)
{
    if (!builder)
        return E_POINTER;
    *builder = nullptr;
    return E_NOTIMPL;
}

DECLSPEC_EXPORT HRESULT WINAPI CreateUriWithFragment(LPCWSTR uri, LPCWSTR fragment, DWORD flags, DWORD_PTR reserved,
                                                    IUri **result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (!uri)
        return E_INVALIDARG;
    if (!fragment)
        return Create(uri, flags, result);
    if (wcschr(uri, '#'))
        return E_INVALIDARG;
    TEXT text = {0};
    AddString(&text, uri);
    if (fragment[0] != '#')
        AddChar(&text, '#');
    AddString(&text, fragment);
    PWSTR joined = Finish(&text);
    if (!joined)
        return E_OUTOFMEMORY;
    HRESULT hr = Create(joined, flags, result);
    UrlFree(joined);
    return hr;
}

/* rfc 3986 5.2.2 reference resolution. */
DECLSPEC_EXPORT HRESULT WINAPI CoInternetCombineIUri(IUri *base, IUri *relative, DWORD flags, IUri **result,
                                                    DWORD_PTR reserved)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (!base || !relative)
        return E_INVALIDARG;
    BSTR scheme = nullptr;
    if (relative->lpVtbl->GetPropertyBSTR(relative, Uri_PROPERTY_SCHEME_NAME, &scheme, 0) == S_OK) {
        SysFreeString(scheme);
        relative->lpVtbl->AddRef(relative);
        *result = relative;
        return S_OK;
    }
    SysFreeString(scheme);
    BSTR parts[6] = {nullptr};
    static const Uri_PROPERTY base_parts[] = {Uri_PROPERTY_SCHEME_NAME, Uri_PROPERTY_AUTHORITY, Uri_PROPERTY_PATH,
                                              Uri_PROPERTY_QUERY};
    for (int i = 0; i < 4; i++)
        base->lpVtbl->GetPropertyBSTR(base, base_parts[i], &parts[i], 0);
    BSTR raw = nullptr;
    relative->lpVtbl->GetPropertyBSTR(relative, Uri_PROPERTY_RAW_URI, &raw, 0);
    TEXT text = {0};
    AddString(&text, parts[0]);
    AddChar(&text, ':');
    LPCWSTR r = raw ? raw : L"";
    if (r[0] == '/' && r[1] == '/') {
        AddString(&text, r);
    } else {
        AddString(&text, L"//");
        AddString(&text, parts[1]);
        if (!r[0]) {
            AddString(&text, parts[2]);
            AddString(&text, parts[3]);
        } else if (r[0] == '#') {
            AddString(&text, parts[2]);
            AddString(&text, parts[3]);
            AddString(&text, r);
        } else if (r[0] == '?') {
            AddString(&text, parts[2]);
            AddString(&text, r);
        } else if (r[0] == '/') {
            AddString(&text, r);
        } else {
            LPCWSTR path = parts[2] ? parts[2] : L"";
            LPCWSTR slash = wcsrchr(path, '/');
            if (slash)
                Add(&text, path, (DWORD)(slash - path + 1));
            else
                AddChar(&text, '/');
            AddString(&text, r);
        }
    }
    PWSTR joined = Finish(&text);
    for (int i = 0; i < 4; i++)
        SysFreeString(parts[i]);
    SysFreeString(raw);
    if (!joined)
        return E_OUTOFMEMORY;
    HRESULT hr = Create(joined, 0, result);
    UrlFree(joined);
    return hr;
}
