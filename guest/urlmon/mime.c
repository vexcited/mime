/*
 * FindMimeFromData: the server's proposed type, the signature of the first 256 bytes, then the registry type of the
 * url's extension, following internet explorer's documented detection.
 */
#include "urlmon_private.h"

#define SNIFF_SIZE 256

typedef struct {
    LPCWSTR Type;
    BOOL Image;
    BOOL (*Match)(const BYTE *data, DWORD size);
} KNOWN_TYPE;

static BOOL Starts(const BYTE *data, DWORD size, const char *prefix, DWORD length)
{
    return size >= length && !memcmp(data, prefix, length);
}

static BOOL IsGif(const BYTE *d, DWORD s)
{
    return Starts(d, s, "GIF87a", 6) || Starts(d, s, "GIF89a", 6);
}

static BOOL IsJpeg(const BYTE *d, DWORD s)
{
    return s >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}

static BOOL IsPng(const BYTE *d, DWORD s)
{
    return Starts(d, s, "\x89PNG\r\n\x1a\n", 8);
}

static BOOL IsBmp(const BYTE *d, DWORD s)
{
    return s >= 14 && d[0] == 'B' && d[1] == 'M' && !d[6] && !d[7] && !d[8] && !d[9];
}

static BOOL IsTiff(const BYTE *d, DWORD s)
{
    return Starts(d, s, "II\x2a\0", 4) || Starts(d, s, "MM\0\x2a", 4);
}

static BOOL IsJg(const BYTE *d, DWORD s)
{
    return s >= 4 && d[0] == 'J' && d[1] == 'G' && d[2] == 4 && d[3] == 14;
}

static BOOL IsEmf(const BYTE *d, DWORD s)
{
    return s >= 44 && d[0] == 1 && !d[1] && !d[2] && !d[3] && !memcmp(d + 40, " EMF", 4);
}

static BOOL IsWmf(const BYTE *d, DWORD s)
{
    return Starts(d, s, "\xd7\xcd\xc6\x9a", 4);
}

static BOOL IsIcon(const BYTE *d, DWORD s)
{
    return s >= 6 && !d[0] && !d[1] && d[2] == 1 && !d[3] && (d[4] || d[5]);
}

static BOOL IsAvi(const BYTE *d, DWORD s)
{
    return s >= 12 && Starts(d, s, "RIFF", 4) && !memcmp(d + 8, "AVI ", 4);
}

static BOOL IsWav(const BYTE *d, DWORD s)
{
    return s >= 12 && Starts(d, s, "RIFF", 4) && !memcmp(d + 8, "WAVE", 4);
}

static BOOL IsAiff(const BYTE *d, DWORD s)
{
    return s >= 12 && Starts(d, s, "FORM", 4) && (!memcmp(d + 8, "AIFF", 4) || !memcmp(d + 8, "AIFC", 4));
}

static BOOL IsAu(const BYTE *d, DWORD s)
{
    return Starts(d, s, ".snd", 4);
}

static BOOL IsMpeg(const BYTE *d, DWORD s)
{
    return s >= 4 && !d[0] && !d[1] && d[2] == 1 && (d[3] == 0xBA || d[3] == 0xB3);
}

static BOOL IsPdf(const BYTE *d, DWORD s)
{
    return Starts(d, s, "%PDF", 4);
}

static BOOL IsPostScript(const BYTE *d, DWORD s)
{
    return Starts(d, s, "%!", 2);
}

static BOOL IsZip(const BYTE *d, DWORD s)
{
    return Starts(d, s, "PK\x03\x04", 4);
}

static BOOL IsGzip(const BYTE *d, DWORD s)
{
    return s >= 2 && d[0] == 0x1F && d[1] == 0x8B;
}

static BOOL IsCompress(const BYTE *d, DWORD s)
{
    return s >= 2 && d[0] == 0x1F && d[1] == 0x9D;
}

static BOOL IsJava(const BYTE *d, DWORD s)
{
    return Starts(d, s, "\xca\xfe\xba\xbe", 4);
}

static BOOL IsExecutable(const BYTE *d, DWORD s)
{
    return Starts(d, s, "MZ", 2);
}

static BOOL IsRichText(const BYTE *d, DWORD s)
{
    return Starts(d, s, "{\\rtf", 5);
}

static BOOL IsXbm(const BYTE *d, DWORD s)
{
    return Starts(d, s, "#define", 7);
}

static BOOL IsBinHex(const BYTE *d, DWORD s)
{
    static const char tag[] = "(This file must be converted with BinHex";
    return Starts(d, s, tag, sizeof(tag) - 1);
}

static BOOL IsBase64(const BYTE *d, DWORD s)
{
    return Starts(d, s, "begin ", 6);
}

static BOOL ContainsTag(const BYTE *d, DWORD s, const char *tag)
{
    DWORD length = (DWORD)strlen(tag);
    for (DWORD i = 0; i + length <= s; i++)
        if (d[i] == '<' && !_strnicmp((const char *)d + i, tag, length))
            return TRUE;
    return FALSE;
}

static BOOL IsHtml(const BYTE *d, DWORD s)
{
    static const char *const tags[] = {"<html", "<head", "<title", "<body", "<script", "<a href", "<pre", "<img",
                                       "<plaintext", "<table", "<!doctype html", "<!--", "<br", "<font", "<p>",
                                       "<frameset", "<iframe", "<h1"};
    for (DWORD i = 0; i < ARRAYSIZE(tags); i++)
        if (ContainsTag(d, s, tags[i]))
            return TRUE;
    return FALSE;
}

static BOOL IsXml(const BYTE *d, DWORD s)
{
    DWORD i = 0;
    while (i < s && (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n'))
        i++;
    return s - i >= 5 && !memcmp(d + i, "<?xml", 5);
}

/* the types internet explorer recognizes in data, in the order it tests them. */
static const KNOWN_TYPE Known[] = {
    {L"text/xml", FALSE, IsXml},
    {L"text/html", FALSE, IsHtml},
    {L"text/richtext", FALSE, IsRichText},
    {L"image/x-xbitmap", TRUE, IsXbm},
    {L"application/postscript", FALSE, IsPostScript},
    {L"application/base64", FALSE, IsBase64},
    {L"application/macbinhex40", FALSE, IsBinHex},
    {L"application/pdf", FALSE, IsPdf},
    {L"audio/x-aiff", FALSE, IsAiff},
    {L"audio/basic", FALSE, IsAu},
    {L"audio/wav", FALSE, IsWav},
    {L"image/gif", TRUE, IsGif},
    {L"image/pjpeg", TRUE, IsJpeg},
    {L"image/tiff", TRUE, IsTiff},
    {L"image/x-png", TRUE, IsPng},
    {L"image/bmp", TRUE, IsBmp},
    {L"image/x-jg", TRUE, IsJg},
    {L"image/x-emf", TRUE, IsEmf},
    {L"image/x-wmf", TRUE, IsWmf},
    {L"image/x-icon", TRUE, IsIcon},
    {L"video/avi", FALSE, IsAvi},
    {L"video/mpeg", FALSE, IsMpeg},
    {L"application/x-compressed", FALSE, IsCompress},
    {L"application/x-zip-compressed", FALSE, IsZip},
    {L"application/x-gzip-compressed", FALSE, IsGzip},
    {L"application/java", FALSE, IsJava},
    {L"application/x-msdownload", FALSE, IsExecutable},
};

/* text when every byte is printable, a tab, a line break, a form feed or escape. */
static BOOL IsText(const BYTE *d, DWORD s)
{
    for (DWORD i = 0; i < s; i++) {
        BYTE c = d[i];
        if (c < 0x20 && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != 0x1B)
            return FALSE;
    }
    return TRUE;
}

/* the newer names FMFD_RETURNUPDATEDIMGMIMES asks for. */
static LPCWSTR Updated(LPCWSTR type, DWORD flags)
{
    if (!(flags & FMFD_RETURNUPDATEDIMGMIMES))
        return type;
    if (!lstrcmpW(type, L"image/x-png"))
        return L"image/png";
    if (!lstrcmpW(type, L"image/pjpeg"))
        return L"image/jpeg";
    return type;
}

static const KNOWN_TYPE *KnownType(LPCWSTR type)
{
    for (DWORD i = 0; i < ARRAYSIZE(Known); i++)
        if (!lstrcmpiW(Known[i].Type, type))
            return &Known[i];
    if (!lstrcmpiW(type, L"image/png"))
        return KnownType(L"image/x-png");
    if (!lstrcmpiW(type, L"image/jpeg") || !lstrcmpiW(type, L"image/jpg"))
        return KnownType(L"image/pjpeg");
    return nullptr;
}

/* the Content Type the classes root gives the url's extension. */
static BOOL TypeOfExtension(LPCWSTR url, PWSTR type, DWORD capacity)
{
    LPCWSTR end = url;
    while (*end && *end != '?' && *end != '#')
        end++;
    LPCWSTR dot = end;
    while (dot > url && dot[-1] != '.' && dot[-1] != '/' && dot[-1] != '\\')
        dot--;
    if (dot == url || dot[-1] != '.' || end - dot > 16)
        return FALSE;
    WCHAR extension[20];
    lstrcpynW(extension, dot - 1, (int)(end - dot + 2));
    DWORD size = capacity * sizeof(WCHAR), kind;
    return !RegGetValueW(HKEY_CLASSES_ROOT, extension, L"Content Type", RRF_RT_REG_SZ, &kind, type, &size);
}

static HRESULT Return(LPCWSTR type, LPWSTR *mime)
{
    SIZE_T bytes = ((SIZE_T)lstrlenW(type) + 1) * sizeof(WCHAR);
    *mime = CoTaskMemAlloc(bytes);
    if (!*mime)
        return E_OUTOFMEMORY;
    memcpy(*mime, type, bytes);
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI FindMimeFromData(LPBC context, LPCWSTR url, LPVOID buffer, DWORD size,
                                               LPCWSTR proposed, DWORD flags, LPWSTR *mime, DWORD reserved)
{
    if (!mime)
        return E_INVALIDARG;
    *mime = nullptr;
    if (!url && !buffer)
        return E_INVALIDARG;
    WCHAR proposed_type[128] = L"";
    if (proposed) {
        lstrcpynW(proposed_type, proposed, ARRAYSIZE(proposed_type));
        PWSTR semicolon = wcschr(proposed_type, ';');
        if (semicolon)
            *semicolon = 0;
        for (DWORD i = (DWORD)lstrlenW(proposed_type); i && proposed_type[i - 1] == ' '; i--)
            proposed_type[i - 1] = 0;
    }
    BOOL has_proposed = proposed_type[0] && lstrcmpiW(proposed_type, L"(null)");
    if (has_proposed && (!lstrcmpiW(proposed_type, L"text/plain") && flags & FMFD_RESPECTTEXTPLAIN))
        return Return(proposed_type, mime);
    if (buffer && size) {
        const BYTE *data = buffer;
        DWORD length = size > SNIFF_SIZE ? SNIFF_SIZE : size;
        const KNOWN_TYPE *sniffed = nullptr;
        for (DWORD i = 0; i < ARRAYSIZE(Known) && !sniffed; i++)
            if (Known[i].Match(data, length))
                sniffed = &Known[i];
        BOOL text = IsText(data, length);
        const KNOWN_TYPE *declared = has_proposed ? KnownType(proposed_type) : nullptr;
        BOOL ambiguous = !has_proposed || !lstrcmpiW(proposed_type, L"text/plain") ||
                         !lstrcmpiW(proposed_type, L"application/octet-stream") ||
                         !lstrcmpiW(proposed_type, L"unknown/unknown") || !lstrcmpiW(proposed_type, L"*/*");
        /* a known proposal stands unless the data is an image of another known type. */
        if (declared && !ambiguous) {
            if (declared->Image && sniffed && sniffed->Image && sniffed != declared)
                return Return(Updated(sniffed->Type, flags), mime);
            if (declared->Image && !lstrcmpiW(proposed_type, declared->Type))
                return Return(Updated(declared->Type, flags), mime);
            return Return(proposed_type, mime);
        }
        if (has_proposed && !ambiguous)
            return Return(proposed_type, mime);
        if (sniffed)
            return Return(Updated(sniffed->Type, flags), mime);
        WCHAR extension_type[128];
        if (url && flags & FMFD_URLASFILENAME && TypeOfExtension(url, extension_type, ARRAYSIZE(extension_type)))
            return Return(extension_type, mime);
        if (has_proposed && !lstrcmpiW(proposed_type, L"text/plain") && !text)
            return Return(L"application/octet-stream", mime);
        if (has_proposed)
            return Return(proposed_type, mime);
        if (url && TypeOfExtension(url, extension_type, ARRAYSIZE(extension_type)))
            return Return(extension_type, mime);
        return Return(text ? L"text/plain" : L"application/octet-stream", mime);
    }
    if (has_proposed)
        return Return(proposed_type, mime);
    WCHAR extension_type[128];
    if (url && TypeOfExtension(url, extension_type, ARRAYSIZE(extension_type)))
        return Return(extension_type, mime);
    return E_FAIL;
}
