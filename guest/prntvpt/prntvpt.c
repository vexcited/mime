/*
 * prntvpt.dll: the print ticket provider, which tells programs what a printer can do as an XML document
 * (the print capabilities) and turns device modes into print tickets and back. programs ask a printer by
 * name; the answers come from its device capabilities and its device mode, which winspool.drv keeps.
 */
#include <mime/win32/objidl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winspool.h>
#include <rtl.h>

/* error messages are optional output: this provider never has any, so the strings stay null. */
typedef WCHAR *BSTR;

#define PTAPI DECLSPEC_EXPORT HRESULT WINAPI

#define PT_JOB_SCOPE 3
#define PT_USER_DEFAULT_DEVMODE 1

typedef struct PROVIDER {
    DWORD Signature;
    WCHAR Printer[128];
} PROVIDER;

#define PROVIDER_SIGNATURE 0x50545650

/* ------------------------------------------------------------------ winspool */

static struct {
    BOOL Tried;
    BOOL(WINAPI *Open)(LPWSTR, LPHANDLE, LPVOID);
    BOOL(WINAPI *Close)(HANDLE);
    LONG(WINAPI *Properties)(HWND, HANDLE, LPWSTR, PDEVMODEW, PDEVMODEW, DWORD);
    int(WINAPI *Capabilities)(LPCWSTR, LPCWSTR, WORD, LPWSTR, const DEVMODEW *);
} g_spool;

static BOOL spool_load(void)
{
    if (!g_spool.Tried) {
        HMODULE module = LoadLibraryW(L"winspool.drv");
        if (module) {
            g_spool.Open = (void *)GetProcAddress(module, "OpenPrinterW");
            g_spool.Close = (void *)GetProcAddress(module, "ClosePrinter");
            g_spool.Properties = (void *)GetProcAddress(module, "DocumentPropertiesW");
            g_spool.Capabilities = (void *)GetProcAddress(module, "DeviceCapabilitiesW");
        }
        g_spool.Tried = TRUE;
    }
    return g_spool.Open && g_spool.Close && g_spool.Properties && g_spool.Capabilities;
}

/* ------------------------------------------------------------------ text */

typedef struct TEXT {
    char *Data;
    SIZE_T Size, Capacity;
    BOOL Failed;
} TEXT;

static void text_add(TEXT *t, const char *s)
{
    SIZE_T n = strlen(s);
    if (t->Failed)
        return;
    if (t->Size + n + 1 > t->Capacity) {
        SIZE_T cap = t->Capacity ? t->Capacity * 2 : 8192;
        while (cap < t->Size + n + 1)
            cap *= 2;
        char *grown = t->Data ? HeapReAlloc(GetProcessHeap(), 0, t->Data, cap) : HeapAlloc(GetProcessHeap(), 0, cap);
        if (!grown) {
            t->Failed = TRUE;
            return;
        }
        t->Data = grown;
        t->Capacity = cap;
    }
    memcpy(t->Data + t->Size, s, n + 1);
    t->Size += n;
}

static void text_addf(TEXT *t, const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    _vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    line[sizeof(line) - 1] = 0;
    text_add(t, line);
}

static HRESULT text_write(TEXT *t, IStream *stream)
{
    if (t->Failed)
        return E_OUTOFMEMORY;
    LARGE_INTEGER zero = {};
    ULONG done;
    stream->lpVtbl->Seek(stream, zero, 0, nullptr);
    HRESULT hr = stream->lpVtbl->Write(stream, t->Data, (ULONG)t->Size, &done);
    ULARGE_INTEGER size;
    size.QuadPart = t->Size;
    stream->lpVtbl->SetSize(stream, size);
    stream->lpVtbl->Seek(stream, zero, 0, nullptr);
    return hr;
}

/* the whole of a stream, from its start, as text; the caller frees it. */
static char *stream_text(IStream *stream)
{
    if (!stream)
        return nullptr;
    LARGE_INTEGER zero = {};
    stream->lpVtbl->Seek(stream, zero, 0, nullptr);
    SIZE_T size = 0, capacity = 4096;
    char *data = HeapAlloc(GetProcessHeap(), 0, capacity);
    for (;;) {
        if (size + 2048 + 1 > capacity) {
            capacity *= 2;
            char *grown = data ? HeapReAlloc(GetProcessHeap(), 0, data, capacity) : nullptr;
            if (!grown) {
                HeapFree(GetProcessHeap(), 0, data);
                return nullptr;
            }
            data = grown;
        }
        ULONG got = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, data + size, 2048, &got);
        size += got;
        if (FAILED(hr) || !got)
            break;
    }
    data[size] = 0;
    stream->lpVtbl->Seek(stream, zero, 0, nullptr);
    return data;
}

/* ------------------------------------------------------------------ the keywords of a print schema */

static const struct {
    WORD Paper;
    const char *Keyword;
} g_media[] = {
    {DMPAPER_LETTER, "NorthAmericaLetter"},       {DMPAPER_LEGAL, "NorthAmericaLegal"},
    {DMPAPER_TABLOID, "NorthAmericaTabloid"},     {DMPAPER_EXECUTIVE, "NorthAmericaExecutive"},
    {DMPAPER_STATEMENT, "NorthAmericaStatement"}, {DMPAPER_A3, "ISOA3"},
    {DMPAPER_A4, "ISOA4"},                        {DMPAPER_A5, "ISOA5"},
    {DMPAPER_A6, "ISOA6"},                        {DMPAPER_B4, "JISB4"},
    {DMPAPER_B5, "JISB5"},                        {DMPAPER_ENV_10, "NorthAmericaNumber10Envelope"},
    {DMPAPER_ENV_DL, "ISODLEnvelope"},            {DMPAPER_ENV_C5, "ISOC5Envelope"},
};

static const char *media_keyword(int paper)
{
    for (int i = 0; i < (int)ARRAYSIZE(g_media); i++)
        if (g_media[i].Paper == paper)
            return g_media[i].Keyword;
    return nullptr;
}

static int media_paper(const char *keyword)
{
    for (int i = 0; i < (int)ARRAYSIZE(g_media); i++)
        if (!strcmp(g_media[i].Keyword, keyword))
            return g_media[i].Paper;
    return 0;
}

static const char g_ns[] =
    "xmlns:psf=\"http://schemas.microsoft.com/windows/2003/08/printing/printschemaframework\" "
    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
    "xmlns:psk=\"http://schemas.microsoft.com/windows/2003/08/printing/printschemakeywords\" version=\"1\"";

static void narrow(const WCHAR *wide, char *out, int size)
{
    if (!WideCharToMultiByte(65001, 0, wide, -1, out, size, nullptr, nullptr))
        out[0] = 0;
    out[size - 1] = 0;
}

/* ------------------------------------------------------------------ the provider */

static PROVIDER *provider_of(void *handle)
{
    PROVIDER *p = handle;
    return p && p->Signature == PROVIDER_SIGNATURE ? p : nullptr;
}

PTAPI PTOpenProviderEx(PCWSTR printer, DWORD max_version, DWORD preferred_version, void **handle, DWORD *used)
{
    if (!handle)
        return E_INVALIDARG;
    *handle = nullptr;
    if (!printer || !*printer || wcslen(printer) >= 128)
        return E_INVALIDARG;
    if (max_version < 1 || preferred_version > max_version)
        return E_INVALIDARG;
    if (!spool_load())
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    HANDLE spool;
    if (!g_spool.Open((LPWSTR)printer, &spool, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    g_spool.Close(spool);
    PROVIDER *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return E_OUTOFMEMORY;
    p->Signature = PROVIDER_SIGNATURE;
    wcscpy(p->Printer, printer);
    *handle = p;
    if (used)
        *used = 1;
    return S_OK;
}

PTAPI PTOpenProvider(PCWSTR printer, DWORD version, void **handle)
{
    return PTOpenProviderEx(printer, version, version, handle, nullptr);
}

PTAPI PTCloseProvider(void *handle)
{
    PROVIDER *p = provider_of(handle);
    if (!p)
        return E_HANDLE;
    p->Signature = 0;
    HeapFree(GetProcessHeap(), 0, p);
    return S_OK;
}

PTAPI PTReleaseMemory(void *memory)
{
    if (!memory)
        return E_INVALIDARG;
    HeapFree(GetProcessHeap(), 0, memory);
    return S_OK;
}

PTAPI PTQuerySchemaVersionSupport(PCWSTR printer, DWORD *version)
{
    if (!printer || !version)
        return E_INVALIDARG;
    *version = 1;
    return S_OK;
}

/* ------------------------------------------------------------------ the capabilities */

static void add_display_name(TEXT *t, const char *name)
{
    text_addf(t, "<psf:Property name=\"psk:DisplayName\"><psf:Value xsi:type=\"xsd:string\">%s</psf:Value></psf:Property>", name);
}

static void capabilities(PROVIDER *p, TEXT *t)
{
    text_addf(t, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<psf:PrintCapabilities %s>\r\n", g_ns);
    text_add(t, "<psf:Feature name=\"psk:DocumentCollate\"><psf:Option name=\"psk:Uncollated\"/></psf:Feature>\r\n");
    text_add(t, "<psf:Feature name=\"psk:JobDuplexAllDocumentsContiguously\"><psf:Option name=\"psk:OneSided\"/></psf:Feature>\r\n");
    text_add(t, "<psf:Feature name=\"psk:JobInputBin\"><psf:Option name=\"psk:AutoSelect\">");
    add_display_name(t, "Automatically Select");
    text_add(t, "</psf:Option></psf:Feature>\r\n");
    text_add(t, "<psf:Feature name=\"psk:PageMediaSize\">");
    add_display_name(t, "Paper size");
    int n = g_spool.Capabilities(p->Printer, nullptr, DC_PAPERS, nullptr, nullptr);
    if (n > 0 && n <= 64) {
        WORD papers[64];
        POINT sizes[64];
        static WCHAR names[64 * 64];
        if (g_spool.Capabilities(p->Printer, nullptr, DC_PAPERS, (LPWSTR)papers, nullptr) == n &&
            g_spool.Capabilities(p->Printer, nullptr, DC_PAPERSIZE, (LPWSTR)sizes, nullptr) == n &&
            g_spool.Capabilities(p->Printer, nullptr, DC_PAPERNAMES, names, nullptr) == n) {
            for (int i = 0; i < n; i++) {
                const char *keyword = media_keyword(papers[i]);
                char name[128];
                if (!keyword)
                    continue;
                narrow(names + 64 * i, name, sizeof(name));
                text_addf(t, "<psf:Option name=\"psk:%s\" constrained=\"psk:None\">", keyword);
                add_display_name(t, name);
                text_addf(t,
                          "<psf:ScoredProperty name=\"psk:MediaSizeWidth\"><psf:Value xsi:type=\"xsd:integer\">%d</psf:Value></psf:ScoredProperty>"
                          "<psf:ScoredProperty name=\"psk:MediaSizeHeight\"><psf:Value xsi:type=\"xsd:integer\">%d</psf:Value></psf:ScoredProperty>",
                          sizes[i].x * 100, sizes[i].y * 100);
                text_add(t, "</psf:Option>");
            }
        }
    }
    text_add(t, "</psf:Feature>\r\n");
    text_add(t, "<psf:Feature name=\"psk:PageOrientation\"><psf:Option name=\"psk:Portrait\"/><psf:Option name=\"psk:Landscape\"/></psf:Feature>\r\n");
    text_add(t, "<psf:Feature name=\"psk:PageOutputColor\"><psf:Option name=\"psk:Color\"/><psf:Option name=\"psk:Grayscale\"/>"
                "<psf:Option name=\"psk:Monochrome\"/></psf:Feature>\r\n");
    text_add(t, "<psf:Feature name=\"psk:PageResolution\">");
    LONG res[16];
    int r = g_spool.Capabilities(p->Printer, nullptr, DC_ENUMRESOLUTIONS, (LPWSTR)res, nullptr);
    for (int i = 0; i < r && i < 8; i++)
        text_addf(t,
                  "<psf:Option name=\"ns0000:Resolution%d\"><psf:ScoredProperty name=\"psk:ResolutionX\"><psf:Value xsi:type=\"xsd:integer\">%d</psf:Value></psf:ScoredProperty>"
                  "<psf:ScoredProperty name=\"psk:ResolutionY\"><psf:Value xsi:type=\"xsd:integer\">%d</psf:Value></psf:ScoredProperty></psf:Option>",
                  (int)res[2 * i], (int)res[2 * i], (int)res[2 * i + 1]);
    text_add(t, "</psf:Feature>\r\n");
    text_add(t, "<psf:ParameterDef name=\"psk:JobCopiesAllDocuments\"><psf:Property name=\"psf:DataType\"><psf:Value xsi:type=\"xsd:QName\">xsd:integer</psf:Value></psf:Property>"
                "<psf:Property name=\"psf:UnitType\"><psf:Value xsi:type=\"xsd:string\">copies</psf:Value></psf:Property>"
                "<psf:Property name=\"psf:DefaultValue\"><psf:Value xsi:type=\"xsd:integer\">1</psf:Value></psf:Property>"
                "<psf:Property name=\"psf:MaxValue\"><psf:Value xsi:type=\"xsd:integer\">1</psf:Value></psf:Property>"
                "<psf:Property name=\"psf:MinValue\"><psf:Value xsi:type=\"xsd:integer\">1</psf:Value></psf:Property></psf:ParameterDef>\r\n");
    text_add(t, "</psf:PrintCapabilities>\r\n");
}

PTAPI PTGetPrintCapabilities(void *handle, IStream *ticket, IStream *capabilities_stream, BSTR *error)
{
    PROVIDER *p = provider_of(handle);
    if (error)
        *error = nullptr;
    if (!p)
        return E_HANDLE;
    if (!capabilities_stream)
        return E_INVALIDARG;
    TEXT t = {};
    capabilities(p, &t);
    HRESULT hr = text_write(&t, capabilities_stream);
    HeapFree(GetProcessHeap(), 0, t.Data);
    return hr;
}

static int number_of(const char *digits)
{
    int n = 0;
    while (*digits >= '0' && *digits <= '9')
        n = n * 10 + (*digits++ - '0');
    return n;
}

/* the device capabilities as a document too, for programs that ask for them. */
PTAPI PTGetPrintDeviceCapabilities(void *handle, IStream *ticket, IStream *capabilities_stream, BSTR *error)
{
    return PTGetPrintCapabilities(handle, ticket, capabilities_stream, error);
}

PTAPI PTGetPrintDeviceResources(void *handle, PCWSTR locale, IStream *ticket, IStream *resources, BSTR *error)
{
    PROVIDER *p = provider_of(handle);
    if (error)
        *error = nullptr;
    if (!p)
        return E_HANDLE;
    if (!resources)
        return E_INVALIDARG;
    TEXT t = {};
    text_addf(&t, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<psf:PrintDeviceResources %s></psf:PrintDeviceResources>\r\n", g_ns);
    HRESULT hr = text_write(&t, resources);
    HeapFree(GetProcessHeap(), 0, t.Data);
    return hr;
}

/* ------------------------------------------------------------------ tickets */

/* the keyword of the first option of a feature in a ticket: "ISOA4" for psk:ISOA4. false when it is not there. */
static BOOL ticket_option(const char *xml, const char *feature, char *keyword, int size)
{
    char pattern[96];
    _snprintf(pattern, sizeof(pattern), "psk:%s", feature);
    const char *at = xml;
    while ((at = strstr(at, "Feature")) != nullptr) {
        const char *quote = strstr(at, "name=");
        if (!quote)
            return FALSE;
        quote += 5;
        if (*quote == '"' || *quote == '\'') {
            char q = *quote++;
            SIZE_T n = strlen(pattern);
            if (!strncmp(quote, pattern, n) && quote[n] == q) {
                const char *next = strstr(quote, "Feature name=");
                const char *option = strstr(quote, "Option");
                if (!option || (next && option > next))
                    return FALSE;
                const char *name = strstr(option, "name=");
                if (!name)
                    return FALSE;
                name += 5;
                char oq = *name++;
                if (strncmp(name, "psk:", 4))
                    return FALSE;
                name += 4;
                int k = 0;
                while (*name && *name != oq && k + 1 < size)
                    keyword[k++] = *name++;
                keyword[k] = 0;
                return TRUE;
            }
        }
        at = quote;
    }
    return FALSE;
}

static BOOL ticket_number(const char *xml, const char *parameter, int *value)
{
    char pattern[96];
    _snprintf(pattern, sizeof(pattern), "psk:%s", parameter);
    const char *at = strstr(xml, pattern);
    if (!at)
        return FALSE;
    const char *v = strstr(at, "Value");
    if (!v)
        return FALSE;
    v = strchr(v, '>');
    if (!v)
        return FALSE;
    v++;
    int n = 0;
    BOOL any = FALSE;
    while (*v >= '0' && *v <= '9') {
        n = n * 10 + (*v++ - '0');
        any = TRUE;
    }
    if (!any)
        return FALSE;
    *value = n;
    return TRUE;
}

/* the settings of a printer, the user's default merged with a device mode. */
static DEVMODEW *settings(PROVIDER *p, const DEVMODEW *input)
{
    HANDLE spool;
    if (!g_spool.Open(p->Printer, &spool, nullptr))
        return nullptr;
    LONG size = g_spool.Properties(nullptr, spool, p->Printer, nullptr, nullptr, 0);
    DEVMODEW *dm = size > 0 ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)size) : nullptr;
    if (dm && g_spool.Properties(nullptr, spool, p->Printer, dm, (PDEVMODEW)input, DM_OUT_BUFFER | (input ? DM_IN_BUFFER : 0)) < 0) {
        HeapFree(GetProcessHeap(), 0, dm);
        dm = nullptr;
    }
    g_spool.Close(spool);
    return dm;
}

PTAPI PTConvertDevModeToPrintTicket(void *handle, ULONG size, PDEVMODEW devmode, int scope, IStream *ticket)
{
    PROVIDER *p = provider_of(handle);
    if (!p)
        return E_HANDLE;
    if (!ticket || !devmode || size < offsetof(DEVMODEW, dmFields) + sizeof(DWORD))
        return E_INVALIDARG;
    TEXT t = {};
    text_addf(&t, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<psf:PrintTicket %s>\r\n", g_ns);
    DWORD f = devmode->dmFields;
    if ((f & DM_COLLATE) && devmode->dmCollate == DMCOLLATE_TRUE)
        text_add(&t, "<psf:Feature name=\"psk:DocumentCollate\"><psf:Option name=\"psk:Collated\"/></psf:Feature>\r\n");
    else if (f & DM_COLLATE)
        text_add(&t, "<psf:Feature name=\"psk:DocumentCollate\"><psf:Option name=\"psk:Uncollated\"/></psf:Feature>\r\n");
    if (f & DM_DUPLEX)
        text_add(&t, "<psf:Feature name=\"psk:JobDuplexAllDocumentsContiguously\"><psf:Option name=\"psk:OneSided\"/></psf:Feature>\r\n");
    const char *media = (f & DM_PAPERSIZE) ? media_keyword(devmode->dmPaperSize) : nullptr;
    if (media)
        text_addf(&t, "<psf:Feature name=\"psk:PageMediaSize\"><psf:Option name=\"psk:%s\"/></psf:Feature>\r\n", media);
    if (f & DM_ORIENTATION)
        text_addf(&t, "<psf:Feature name=\"psk:PageOrientation\"><psf:Option name=\"psk:%s\"/></psf:Feature>\r\n",
                  devmode->dmOrientation == DMORIENT_LANDSCAPE ? "Landscape" : "Portrait");
    if (f & DM_COLOR)
        text_addf(&t, "<psf:Feature name=\"psk:PageOutputColor\"><psf:Option name=\"psk:%s\"/></psf:Feature>\r\n",
                  devmode->dmColor == DMCOLOR_MONOCHROME ? "Grayscale" : "Color");
    if ((f & DM_PRINTQUALITY) && devmode->dmPrintQuality > 0)
        text_addf(&t, "<psf:Feature name=\"psk:PageResolution\"><psf:Option name=\"ns0000:Resolution%d\"/></psf:Feature>\r\n",
                  devmode->dmPrintQuality);
    if ((f & DM_COPIES) && devmode->dmCopies > 0)
        text_addf(&t, "<psf:ParameterInit name=\"psk:JobCopiesAllDocuments\"><psf:Value xsi:type=\"xsd:integer\">%d</psf:Value></psf:ParameterInit>\r\n",
                  devmode->dmCopies);
    text_add(&t, "</psf:PrintTicket>\r\n");
    HRESULT hr = text_write(&t, ticket);
    HeapFree(GetProcessHeap(), 0, t.Data);
    return hr;
}

PTAPI PTConvertPrintTicketToDevMode(void *handle, IStream *ticket, int base, int scope, ULONG *size,
                                    PDEVMODEW *devmode, BSTR *error)
{
    PROVIDER *p = provider_of(handle);
    if (error)
        *error = nullptr;
    if (!p)
        return E_HANDLE;
    if (!ticket || !size || !devmode)
        return E_INVALIDARG;
    *devmode = nullptr;
    *size = 0;
    char *xml = stream_text(ticket);
    if (!xml)
        return E_OUTOFMEMORY;
    DEVMODEW *dm = settings(p, nullptr);
    if (!dm) {
        HeapFree(GetProcessHeap(), 0, xml);
        return HRESULT_FROM_WIN32(ERROR_INVALID_PRINTER_NAME);
    }
    char keyword[96];
    int value;
    if (ticket_option(xml, "PageMediaSize", keyword, sizeof(keyword))) {
        int paper = media_paper(keyword);
        if (paper) {
            dm->dmFields |= DM_PAPERSIZE;
            dm->dmPaperSize = (short)paper;
            dm->dmFormName[0] = 0;
        }
    }
    if (ticket_option(xml, "PageOrientation", keyword, sizeof(keyword))) {
        dm->dmFields |= DM_ORIENTATION;
        dm->dmOrientation = !strcmp(keyword, "Landscape") || !strcmp(keyword, "ReverseLandscape") ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    }
    if (ticket_option(xml, "PageOutputColor", keyword, sizeof(keyword))) {
        dm->dmFields |= DM_COLOR;
        dm->dmColor = !strcmp(keyword, "Color") ? DMCOLOR_COLOR : DMCOLOR_MONOCHROME;
    }
    if (ticket_option(xml, "DocumentCollate", keyword, sizeof(keyword))) {
        dm->dmFields |= DM_COLLATE;
        dm->dmCollate = !strcmp(keyword, "Collated") ? DMCOLLATE_TRUE : DMCOLLATE_FALSE;
    }
    if (ticket_option(xml, "PageResolution", keyword, sizeof(keyword))) {
        const char *digits = keyword;
        while (*digits && (*digits < '0' || *digits > '9'))
            digits++;
        if (*digits) {
            dm->dmFields |= DM_PRINTQUALITY | DM_YRESOLUTION;
            dm->dmPrintQuality = dm->dmYResolution = (short)number_of(digits);
        }
    }
    if (ticket_number(xml, "JobCopiesAllDocuments", &value) && value > 0) {
        dm->dmFields |= DM_COPIES;
        dm->dmCopies = (short)value;
    }
    HeapFree(GetProcessHeap(), 0, xml);
    /* the device mode is checked against the printer's once more, which drops what it cannot do. */
    DEVMODEW *checked = settings(p, dm);
    if (checked) {
        HeapFree(GetProcessHeap(), 0, dm);
        dm = checked;
    }
    *devmode = dm;
    *size = (ULONG)(dm->dmSize + dm->dmDriverExtra);
    return S_OK;
}

PTAPI PTMergeAndValidatePrintTicket(void *handle, IStream *base, IStream *delta, int scope, IStream *result,
                                    BSTR *error)
{
    PROVIDER *p = provider_of(handle);
    if (error)
        *error = nullptr;
    if (!p)
        return E_HANDLE;
    if (!base || !result)
        return E_INVALIDARG;
    char *xml_base = stream_text(base);
    char *xml_delta = stream_text(delta);
    HRESULT hr = E_OUTOFMEMORY;
    if (xml_base) {
        /* the settings of both go through the device mode of the printer, so what is left is what it supports. */
        DEVMODEW *dm = settings(p, nullptr);
        TEXT t = {};
        if (dm) {
            const char *sources[2] = {xml_base, xml_delta};
            for (int s = 0; s < 2; s++) {
                const char *xml = sources[s];
                char keyword[96];
                int value;
                if (!xml)
                    continue;
                if (ticket_option(xml, "PageMediaSize", keyword, sizeof(keyword)) && media_paper(keyword)) {
                    dm->dmPaperSize = (short)media_paper(keyword);
                    dm->dmFormName[0] = 0;
                }
                if (ticket_option(xml, "PageOrientation", keyword, sizeof(keyword)))
                    dm->dmOrientation = !strcmp(keyword, "Landscape") ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                if (ticket_option(xml, "PageOutputColor", keyword, sizeof(keyword)))
                    dm->dmColor = !strcmp(keyword, "Color") ? DMCOLOR_COLOR : DMCOLOR_MONOCHROME;
                if (ticket_option(xml, "DocumentCollate", keyword, sizeof(keyword)))
                    dm->dmCollate = !strcmp(keyword, "Collated") ? DMCOLLATE_TRUE : DMCOLLATE_FALSE;
                if (ticket_number(xml, "JobCopiesAllDocuments", &value) && value > 0)
                    dm->dmCopies = (short)value;
            }
            DEVMODEW *checked = settings(p, dm);
            if (checked) {
                HeapFree(GetProcessHeap(), 0, dm);
                dm = checked;
            }
            hr = PTConvertDevModeToPrintTicket(handle, (ULONG)(dm->dmSize + dm->dmDriverExtra), dm, scope, result);
            HeapFree(GetProcessHeap(), 0, dm);
        } else {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_PRINTER_NAME);
        }
        HeapFree(GetProcessHeap(), 0, t.Data);
    }
    HeapFree(GetProcessHeap(), 0, xml_base);
    HeapFree(GetProcessHeap(), 0, xml_delta);
    return hr;
}

DECLSPEC_EXPORT BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}
