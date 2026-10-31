/*
 * mlang.dll: the multi language object that programs ask for to convert text between code pages, and the conversion
 * functions it exports. the object is IMultiLanguage; the code pages it knows are the ones of the table below that
 * the machine has, and the text is converted through the national language support of the system. listing the code
 * pages and the languages, and converters made one at a time, are not here.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <rtl.h>

#define MLANG DECLSPEC_EXPORT
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110L)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

/* ------------------------------------------------------------------ code pages */

typedef struct MIMECPINFO {
    DWORD dwFlags;
    UINT uiCodePage;
    UINT uiFamilyCodePage;
    WCHAR wszDescription[64];
    WCHAR wszWebCharset[50];
    WCHAR wszHeaderCharset[50];
    WCHAR wszBodyCharset[50];
    WCHAR wszFixedWidthFont[32];
    WCHAR wszProportionalFont[32];
    BYTE bGDICharset;
} MIMECPINFO;

typedef struct MIMECSETINFO {
    UINT uiCodePage;
    UINT uiInternetEncoding;
    WCHAR wszCharset[50];
} MIMECSETINFO;

#define MIMECONTF_MAILNEWS 0x00000001
#define MIMECONTF_BROWSER 0x00000002
#define MIMECONTF_MINIMAL 0x00000004
#define MIMECONTF_IMPORT 0x00000008
#define MIMECONTF_SAVABLE_MAILNEWS 0x00000100
#define MIMECONTF_SAVABLE_BROWSER 0x00000200
#define MIMECONTF_EXPORT 0x00000400
#define MIMECONTF_VALID 0x00020000
#define MIMECONTF_VALID_NLS 0x00040000

#define UNICODE_LE 1200
#define UNICODE_BE 1201
#define UTF8 65001

static const struct {
    UINT page;
    UINT family;
    PCWSTR description;
    PCWSTR charset;
    BYTE gdi;
} g_pages[] = {
    {437, 1252, L"OEM United States", L"ibm437", 0},
    {708, 1256, L"Arabic (ASMO 708)", L"asmo-708", 178},
    {720, 1256, L"Arabic (DOS)", L"dos-720", 178},
    {737, 1253, L"Greek (DOS)", L"ibm737", 161},
    {775, 1257, L"Baltic (DOS)", L"ibm775", 186},
    {850, 1252, L"Western European (DOS)", L"ibm850", 0},
    {852, 1250, L"Central European (DOS)", L"ibm852", 238},
    {855, 1251, L"Cyrillic (DOS)", L"ibm855", 204},
    {857, 1254, L"Turkish (DOS)", L"ibm857", 162},
    {860, 1252, L"Portuguese (DOS)", L"ibm860", 0},
    {861, 1252, L"Icelandic (DOS)", L"ibm861", 0},
    {862, 1255, L"Hebrew (DOS)", L"dos-862", 177},
    {863, 1252, L"French Canadian (DOS)", L"ibm863", 0},
    {865, 1252, L"Nordic (DOS)", L"ibm865", 0},
    {866, 1251, L"Cyrillic (DOS)", L"cp866", 204},
    {869, 1253, L"Greek, Modern (DOS)", L"ibm869", 161},
    {874, 874, L"Thai (Windows)", L"windows-874", 222},
    {932, 932, L"Japanese (Shift-JIS)", L"shift_jis", 128},
    {936, 936, L"Chinese Simplified (GB2312)", L"gb2312", 134},
    {949, 949, L"Korean", L"ks_c_5601-1987", 129},
    {950, 950, L"Chinese Traditional (Big5)", L"big5", 136},
    {1200, 1200, L"Unicode", L"utf-16", 0},
    {1201, 1200, L"Unicode (Big-Endian)", L"unicodefffe", 0},
    {1250, 1250, L"Central European (Windows)", L"windows-1250", 238},
    {1251, 1251, L"Cyrillic (Windows)", L"windows-1251", 204},
    {1252, 1252, L"Western European (Windows)", L"windows-1252", 0},
    {1253, 1253, L"Greek (Windows)", L"windows-1253", 161},
    {1254, 1254, L"Turkish (Windows)", L"windows-1254", 162},
    {1255, 1255, L"Hebrew (Windows)", L"windows-1255", 177},
    {1256, 1256, L"Arabic (Windows)", L"windows-1256", 178},
    {1257, 1257, L"Baltic (Windows)", L"windows-1257", 186},
    {1258, 1258, L"Vietnamese (Windows)", L"windows-1258", 163},
    {10000, 1252, L"Western European (Mac)", L"macintosh", 77},
    {20127, 1252, L"US-ASCII", L"us-ascii", 0},
    {20866, 1251, L"Cyrillic (KOI8-R)", L"koi8-r", 204},
    {21866, 1251, L"Cyrillic (KOI8-U)", L"koi8-u", 204},
    {28591, 1252, L"Western European (ISO)", L"iso-8859-1", 0},
    {28592, 1250, L"Central European (ISO)", L"iso-8859-2", 238},
    {28593, 1254, L"Latin 3 (ISO)", L"iso-8859-3", 162},
    {28594, 1257, L"Baltic (ISO)", L"iso-8859-4", 186},
    {28595, 1251, L"Cyrillic (ISO)", L"iso-8859-5", 204},
    {28596, 1256, L"Arabic (ISO)", L"iso-8859-6", 178},
    {28597, 1253, L"Greek (ISO)", L"iso-8859-7", 161},
    {28598, 1255, L"Hebrew (ISO-Visual)", L"iso-8859-8", 177},
    {28599, 1254, L"Turkish (ISO)", L"iso-8859-9", 162},
    {28605, 1252, L"Latin 9 (ISO)", L"iso-8859-15", 0},
    {50220, 932, L"Japanese (JIS)", L"iso-2022-jp", 128},
    {51932, 932, L"Japanese (EUC)", L"euc-jp", 128},
    {51936, 936, L"Chinese Simplified (EUC)", L"euc-cn", 134},
    {51949, 949, L"Korean (EUC)", L"euc-kr", 129},
    {54936, 936, L"Chinese Simplified (GB18030)", L"gb18030", 134},
    {65000, 1252, L"Unicode (UTF-7)", L"utf-7", 0},
    {65001, 1200, L"Unicode (UTF-8)", L"utf-8", 0},
};

static BOOL page_known(UINT page)
{
    return page == UNICODE_LE || page == UNICODE_BE || IsValidCodePage(page);
}

static int find_page(UINT page)
{
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_pages); i++)
        if (g_pages[i].page == page)
            return i;
    return -1;
}

static void copy_text(WCHAR *to, int chars, PCWSTR from)
{
    lstrcpynW(to, from, chars);
}

/* ------------------------------------------------------------------ conversion */

/* the bytes of the end of the text that start a character the text does not finish. */
static UINT incomplete_tail(UINT page, const BYTE *text, UINT size)
{
    if (page == UTF8) {
        for (UINT back = 1; back <= 3 && back <= size; back++) {
            BYTE c = text[size - back];
            if ((c & 0xC0) == 0x80)
                continue;
            UINT need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            return need > back ? back : 0;
        }
        return 0;
    }
    if (page == UNICODE_LE || page == UNICODE_BE)
        return size & 1;
    CPINFO info;
    if (!GetCPInfo(page, &info) || info.MaxCharSize < 2)
        return 0;
    UINT at = 0;
    while (at < size)
        at += IsDBCSLeadByteEx(page, text[at]) ? 2 : 1;
    return at > size ? 1 : 0;
}

/* the number of characters in the first bytes of text, less a character cut at their end. */
static UINT whole_chars(UINT page, const BYTE *text, UINT bytes)
{
    bytes -= incomplete_tail(page, text, bytes);
    if (page == UNICODE_LE || page == UNICODE_BE)
        return bytes / 2;
    return bytes ? (UINT)MultiByteToWideChar(page, 0, (LPCCH)text, (int)bytes, nullptr, 0) : 0;
}

/*
 * text of a code page to utf-16. a character cut at the end of the bytes is kept in the state, the count in the low
 * byte and the bytes above it, and made whole by the next call. what does not fit in the destination is not
 * consumed. S_FALSE when the code page is not known.
 */
static HRESULT to_unicode(DWORD *state, UINT page, const BYTE *source, UINT *source_size, WCHAR *dest, UINT *dest_size)
{
    if (!page_known(page))
        return S_FALSE;
    UINT held = state ? (*state & 0xFF) : 0, size = source_size ? *source_size : 0;
    if (held > 3 || size > (1 << 20) || (size && !source))
        return E_INVALIDARG;
    BYTE *joined = HeapAlloc(GetProcessHeap(), 0, held + size + 1);
    if (!joined)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < held; i++)
        joined[i] = (BYTE)(*state >> (8 * (i + 1)));
    if (size)
        memcpy(joined + held, source, size);
    UINT total = held + size, keep = incomplete_tail(page, joined, total), whole = total - keep, used = whole;
    UINT capacity = dest_size ? *dest_size : 0, chars = whole_chars(page, joined, whole);
    if (dest && chars > capacity) {
        /* the longest start that fits, found by halving. */
        UINT low = 0, high = whole;
        while (low < high) {
            UINT middle = (low + high + 1) / 2;
            if (whole_chars(page, joined, middle) <= capacity)
                low = middle;
            else
                high = middle - 1;
        }
        used = low - incomplete_tail(page, joined, low);
        chars = whole_chars(page, joined, used);
    }
    if (dest && chars) {
        if (page == UNICODE_LE || page == UNICODE_BE) {
            for (UINT i = 0; i < chars; i++)
                dest[i] = page == UNICODE_LE ? (WCHAR)(joined[2 * i] | joined[2 * i + 1] << 8)
                                             : (WCHAR)(joined[2 * i] << 8 | joined[2 * i + 1]);
        } else {
            MultiByteToWideChar(page, 0, (LPCCH)joined, (int)used, dest, (int)chars);
        }
    }
    UINT consumed;
    DWORD next = state ? *state : 0;
    if (used == whole) {
        consumed = size;
        next = keep;
        for (UINT i = 0; i < keep; i++)
            next |= (DWORD)joined[whole + i] << (8 * (i + 1));
    } else if (used >= held && used) {
        consumed = used - held;
        next = 0;
    } else {
        consumed = 0;
    }
    HeapFree(GetProcessHeap(), 0, joined);
    if (state)
        *state = next;
    if (source_size)
        *source_size = consumed;
    if (dest_size)
        *dest_size = chars;
    return S_OK;
}

static HRESULT from_unicode(DWORD *state, UINT page, const WCHAR *source, UINT *source_size, BYTE *dest, UINT *dest_size)
{
    if (!page_known(page))
        return S_FALSE;
    UINT chars = source_size ? *source_size : 0;
    if (chars && !source)
        return E_INVALIDARG;
    /* a high surrogate at the end waits for its pair in the next call. */
    UINT whole = chars;
    if (whole && source[whole - 1] >= 0xD800 && source[whole - 1] < 0xDC00)
        whole--;
    UINT bytes;
    if (page == UNICODE_LE || page == UNICODE_BE) {
        bytes = whole * 2;
        if (dest && dest_size && bytes > *dest_size) {
            whole = *dest_size / 2;
            bytes = whole * 2;
        }
        if (dest)
            for (UINT i = 0; i < whole; i++) {
                dest[2 * i] = page == UNICODE_LE ? (BYTE)source[i] : (BYTE)(source[i] >> 8);
                dest[2 * i + 1] = page == UNICODE_LE ? (BYTE)(source[i] >> 8) : (BYTE)source[i];
            }
    } else if (!dest) {
        bytes = whole ? (UINT)WideCharToMultiByte(page, 0, source, (int)whole, nullptr, 0, nullptr, nullptr) : 0;
    } else {
        UINT capacity = dest_size ? *dest_size : 0;
        int need = whole ? WideCharToMultiByte(page, 0, source, (int)whole, nullptr, 0, nullptr, nullptr) : 0;
        while (need > (int)capacity && whole) {
            whole -= (whole + 3) / 4;
            if (whole && source[whole - 1] >= 0xD800 && source[whole - 1] < 0xDC00)
                whole--;
            need = whole ? WideCharToMultiByte(page, 0, source, (int)whole, nullptr, 0, nullptr, nullptr) : 0;
        }
        bytes = need > 0 ? (UINT)WideCharToMultiByte(page, 0, source, (int)whole, (LPSTR)dest, (int)capacity, nullptr,
                                                     nullptr)
                         : 0;
    }
    if (state)
        *state = 0;
    if (source_size)
        *source_size = whole;
    if (dest_size)
        *dest_size = bytes;
    return S_OK;
}

static HRESULT convert(DWORD *state, UINT from, UINT to, const BYTE *source, UINT *source_size, BYTE *dest,
                       UINT *dest_size)
{
    if (!page_known(from) || !page_known(to))
        return S_FALSE;
    UINT size = source_size ? *source_size : 0;
    if (from == to) {
        UINT room = dest_size ? *dest_size : 0;
        UINT n = dest && size > room ? room : size;
        if (dest && n)
            memcpy(dest, source, n);
        if (source_size)
            *source_size = n;
        if (dest_size)
            *dest_size = n;
        return S_OK;
    }
    /* through utf-16: the source is read whole, and what the destination holds is written. */
    UINT chars = size + 4, used = size;
    WCHAR *middle = HeapAlloc(GetProcessHeap(), 0, chars * sizeof(WCHAR));
    if (!middle)
        return E_OUTOFMEMORY;
    DWORD local = state ? *state : 0;
    HRESULT hr = to_unicode(&local, from, source, &used, middle, &chars);
    if (hr == S_OK) {
        UINT converted = chars;
        hr = from_unicode(nullptr, to, middle, &converted, dest, dest_size);
        if (hr == S_OK && converted < chars) {
            /* not all of it fitted: the source consumed is the bytes of the characters that did. */
            UINT back = 0;
            from_unicode(nullptr, from, middle, &converted, nullptr, &back);
            used = back;
        }
        if (hr == S_OK && state && converted == chars)
            *state = local;
        if (hr == S_OK && source_size)
            *source_size = used;
    }
    HeapFree(GetProcessHeap(), 0, middle);
    return hr;
}

MLANG HRESULT WINAPI ConvertINetMultiByteToUnicode(LPDWORD state, DWORD page, LPCSTR source, LPINT source_size,
                                                   LPWSTR dest, LPINT dest_size)
{
    UINT in = source_size ? (UINT)*source_size : 0, out = dest_size ? (UINT)*dest_size : 0;
    if (source_size && *source_size < 0 && source)
        in = 0;
    HRESULT hr = to_unicode(state, page, (const BYTE *)source, &in, dest, &out);
    if (source_size)
        *source_size = (int)in;
    if (dest_size)
        *dest_size = (int)out;
    return hr;
}

MLANG HRESULT WINAPI ConvertINetUnicodeToMultiByte(LPDWORD state, DWORD page, LPCWSTR source, LPINT source_size,
                                                   LPSTR dest, LPINT dest_size)
{
    UINT in = source_size ? (UINT)*source_size : 0, out = dest_size ? (UINT)*dest_size : 0;
    HRESULT hr = from_unicode(state, page, source, &in, (BYTE *)dest, &out);
    if (source_size)
        *source_size = (int)in;
    if (dest_size)
        *dest_size = (int)out;
    return hr;
}

MLANG HRESULT WINAPI ConvertINetString(LPDWORD state, DWORD from, DWORD to, LPCSTR source, LPINT source_size,
                                       LPSTR dest, LPINT dest_size)
{
    UINT in = source_size ? (UINT)*source_size : 0, out = dest_size ? (UINT)*dest_size : 0;
    HRESULT hr = convert(state, from, to, (const BYTE *)source, &in, (BYTE *)dest, &out);
    if (source_size)
        *source_size = (int)in;
    if (dest_size)
        *dest_size = (int)out;
    return hr;
}

MLANG HRESULT WINAPI IsConvertINetStringAvailable(DWORD from, DWORD to)
{
    return page_known(from) && page_known(to) ? S_OK : S_FALSE;
}

/* ------------------------------------------------------------------ IMultiLanguage */

typedef struct IMultiLanguage IMultiLanguage;

typedef struct IMultiLanguageVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IMultiLanguage *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IMultiLanguage *);
    ULONG(STDMETHODCALLTYPE *Release)(IMultiLanguage *);
    HRESULT(STDMETHODCALLTYPE *GetNumberOfCodePageInfo)(IMultiLanguage *, UINT *);
    HRESULT(STDMETHODCALLTYPE *GetCodePageInfo)(IMultiLanguage *, UINT, MIMECPINFO *);
    HRESULT(STDMETHODCALLTYPE *GetFamilyCodePage)(IMultiLanguage *, UINT, UINT *);
    HRESULT(STDMETHODCALLTYPE *EnumCodePages)(IMultiLanguage *, DWORD, void **);
    HRESULT(STDMETHODCALLTYPE *GetCharsetInfo)(IMultiLanguage *, BSTR, MIMECSETINFO *);
    HRESULT(STDMETHODCALLTYPE *IsConvertible)(IMultiLanguage *, DWORD, DWORD);
    HRESULT(STDMETHODCALLTYPE *ConvertString)(IMultiLanguage *, DWORD *, DWORD, DWORD, BYTE *, UINT *, BYTE *, UINT *);
    HRESULT(STDMETHODCALLTYPE *ConvertStringToUnicode)(IMultiLanguage *, DWORD *, DWORD, CHAR *, UINT *, WCHAR *, UINT *);
    HRESULT(STDMETHODCALLTYPE *ConvertStringFromUnicode)(IMultiLanguage *, DWORD *, DWORD, WCHAR *, UINT *, CHAR *,
                                                         UINT *);
    HRESULT(STDMETHODCALLTYPE *ConvertStringReset)(IMultiLanguage *);
    HRESULT(STDMETHODCALLTYPE *GetRfc1766FromLcid)(IMultiLanguage *, LCID, BSTR *);
    HRESULT(STDMETHODCALLTYPE *GetLcidFromRfc1766)(IMultiLanguage *, LCID *, BSTR);
    HRESULT(STDMETHODCALLTYPE *EnumRfc1766)(IMultiLanguage *, void **);
    HRESULT(STDMETHODCALLTYPE *GetRfc1766Info)(IMultiLanguage *, LCID, void *);
    HRESULT(STDMETHODCALLTYPE *CreateConvertCharset)(IMultiLanguage *, UINT, UINT, DWORD, void **);
} IMultiLanguageVtbl;

struct IMultiLanguage {
    const IMultiLanguageVtbl *lpVtbl;
    LONG refs;
};

MIME_DEFINE_GUID(CLSID_CMultiLanguage, 0x275c23e2, 0x3747, 0x11d0, 0x9f, 0xea, 0x00, 0xaa, 0x00, 0x3f, 0x86, 0x46);
MIME_DEFINE_GUID(IID_IMultiLanguage, 0x275c23e1, 0x3747, 0x11d0, 0x9f, 0xea, 0x00, 0xaa, 0x00, 0x3f, 0x86, 0x46);

static HRESULT STDMETHODCALLTYPE ml_query_interface(IMultiLanguage *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IMultiLanguage))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&This->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ml_add_ref(IMultiLanguage *This)
{
    return (ULONG)InterlockedIncrement(&This->refs);
}

static ULONG STDMETHODCALLTYPE ml_release(IMultiLanguage *This)
{
    LONG refs = InterlockedDecrement(&This->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE ml_count(IMultiLanguage *This, UINT *count)
{
    if (!count)
        return E_INVALIDARG;
    UINT n = 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(g_pages); i++)
        n += page_known(g_pages[i].page);
    *count = n;
    return S_OK;
}

static void fill_info(MIMECPINFO *info, int i)
{
    memset(info, 0, sizeof(*info));
    info->dwFlags = MIMECONTF_MAILNEWS | MIMECONTF_BROWSER | MIMECONTF_MINIMAL | MIMECONTF_IMPORT |
                    MIMECONTF_SAVABLE_MAILNEWS | MIMECONTF_SAVABLE_BROWSER | MIMECONTF_EXPORT | MIMECONTF_VALID |
                    MIMECONTF_VALID_NLS;
    info->uiCodePage = g_pages[i].page;
    info->uiFamilyCodePage = g_pages[i].family;
    copy_text(info->wszDescription, RTL_NUMBER_OF(info->wszDescription), g_pages[i].description);
    copy_text(info->wszWebCharset, RTL_NUMBER_OF(info->wszWebCharset), g_pages[i].charset);
    copy_text(info->wszHeaderCharset, RTL_NUMBER_OF(info->wszHeaderCharset), g_pages[i].charset);
    copy_text(info->wszBodyCharset, RTL_NUMBER_OF(info->wszBodyCharset), g_pages[i].charset);
    copy_text(info->wszFixedWidthFont, RTL_NUMBER_OF(info->wszFixedWidthFont), L"Courier New");
    copy_text(info->wszProportionalFont, RTL_NUMBER_OF(info->wszProportionalFont), L"Arial");
    info->bGDICharset = g_pages[i].gdi;
}

static HRESULT STDMETHODCALLTYPE ml_code_page_info(IMultiLanguage *This, UINT page, MIMECPINFO *info)
{
    if (!info)
        return E_INVALIDARG;
    int i = find_page(page);
    if (i < 0 || !page_known(page))
        return E_FAIL;
    fill_info(info, i);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ml_family(IMultiLanguage *This, UINT page, UINT *family)
{
    if (!family)
        return E_INVALIDARG;
    int i = find_page(page);
    if (i < 0 || !page_known(page))
        return E_FAIL;
    *family = g_pages[i].family;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ml_enum_pages(IMultiLanguage *This, DWORD flags, void **list)
{
    if (list)
        *list = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ml_charset_info(IMultiLanguage *This, BSTR charset, MIMECSETINFO *info)
{
    if (!charset || !info)
        return E_INVALIDARG;
    for (UINT i = 0; i < RTL_NUMBER_OF(g_pages); i++) {
        if (CompareStringOrdinal(charset, -1, g_pages[i].charset, -1, TRUE) != CSTR_EQUAL || !page_known(g_pages[i].page))
            continue;
        info->uiCodePage = g_pages[i].page;
        info->uiInternetEncoding = g_pages[i].page;
        copy_text(info->wszCharset, RTL_NUMBER_OF(info->wszCharset), g_pages[i].charset);
        return S_OK;
    }
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE ml_is_convertible(IMultiLanguage *This, DWORD from, DWORD to)
{
    return IsConvertINetStringAvailable(from, to);
}

static HRESULT STDMETHODCALLTYPE ml_convert_string(IMultiLanguage *This, DWORD *state, DWORD from, DWORD to, BYTE *source,
                                                   UINT *source_size, BYTE *dest, UINT *dest_size)
{
    return convert(state, from, to, source, source_size, dest, dest_size);
}

static HRESULT STDMETHODCALLTYPE ml_to_unicode(IMultiLanguage *This, DWORD *state, DWORD page, CHAR *source,
                                               UINT *source_size, WCHAR *dest, UINT *dest_size)
{
    return to_unicode(state, page, (const BYTE *)source, source_size, dest, dest_size);
}

static HRESULT STDMETHODCALLTYPE ml_from_unicode(IMultiLanguage *This, DWORD *state, DWORD page, WCHAR *source,
                                                 UINT *source_size, CHAR *dest, UINT *dest_size)
{
    return from_unicode(state, page, source, source_size, (BYTE *)dest, dest_size);
}

static HRESULT STDMETHODCALLTYPE ml_reset(IMultiLanguage *This)
{
    return S_OK;
}

/* the name of a locale as a language tag in lower case, as the language and the country. */
static HRESULT STDMETHODCALLTYPE ml_rfc1766_from_lcid(IMultiLanguage *This, LCID locale, BSTR *name)
{
    if (!name)
        return E_INVALIDARG;
    *name = nullptr;
    WCHAR language[16], country[16], tag[40];
    if (!GetLocaleInfoW(locale, LOCALE_SISO639LANGNAME, language, RTL_NUMBER_OF(language)))
        return E_FAIL;
    BOOL has_country = (locale >> 10) != 0 && GetLocaleInfoW(locale, LOCALE_SISO3166CTRYNAME, country, RTL_NUMBER_OF(country));
    if (has_country)
        _snwprintf(tag, RTL_NUMBER_OF(tag), L"%ls-%ls", language, country);
    else
        lstrcpynW(tag, language, RTL_NUMBER_OF(tag));
    for (WCHAR *c = tag; *c; c++)
        *c = (WCHAR)towlower(*c);
    *name = SysAllocString(tag);
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE ml_lcid_from_rfc1766(IMultiLanguage *This, LCID *locale, BSTR name)
{
    if (!locale || !name)
        return E_INVALIDARG;
    LCID found = LocaleNameToLCID(name, 0);
    if (!found)
        return E_FAIL;
    *locale = found;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ml_enum_rfc1766(IMultiLanguage *This, void **list)
{
    if (list)
        *list = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ml_rfc1766_info(IMultiLanguage *This, LCID locale, void *info)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE ml_create_convert_charset(IMultiLanguage *This, UINT from, UINT to, DWORD properties,
                                                           void **converter)
{
    if (converter)
        *converter = nullptr;
    return E_NOTIMPL;
}

static const IMultiLanguageVtbl g_multi_language = {
    ml_query_interface, ml_add_ref,      ml_release,         ml_count,           ml_code_page_info,
    ml_family,          ml_enum_pages,   ml_charset_info,    ml_is_convertible,  ml_convert_string,
    ml_to_unicode,      ml_from_unicode, ml_reset,           ml_rfc1766_from_lcid, ml_lcid_from_rfc1766,
    ml_enum_rfc1766,    ml_rfc1766_info, ml_create_convert_charset};

/* ------------------------------------------------------------------ the class factory */

static HRESULT STDMETHODCALLTYPE f_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE f_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE f_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE f_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    IMultiLanguage *language = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*language));
    if (!language)
        return E_OUTOFMEMORY;
    language->lpVtbl = &g_multi_language;
    language->refs = 1;
    HRESULT hr = ml_query_interface(language, riid, object);
    ml_release(language);
    return hr;
}

static HRESULT STDMETHODCALLTYPE f_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {f_query_interface, f_add_ref, f_release, f_create_instance,
                                                 f_lock_server};
static IClassFactory g_factory = {(IClassFactoryVtbl *)&g_factory_vtbl};

MLANG HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualGUID(clsid, &CLSID_CMultiLanguage))
        return CLASS_E_CLASSNOTAVAILABLE;
    return f_query_interface(&g_factory, riid, object);
}

MLANG HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}
