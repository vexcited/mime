/*
 * built-in printers, papers and resolutions. printers cannot be installed or removed. the default printer and each
 * user's device modes are stored in the Windows registry locations.
 */
#include "winspool_private.h"
#include <stddef.h>

/* ------------------------------------------------------------------ the printers */

static const SpoolPrinter g_printers[] = {
    {L"Microsoft Print to PDF", L"Microsoft Print To PDF", L"PORTPROMPT:", L"", L"", L"Microsoft",
     L"Microsoft Print To PDF", PRINTER_ATTRIBUTE_LOCAL | PRINTER_ATTRIBUTE_QUEUED | PRINTER_ATTRIBUTE_ENABLE_DEVQ},
};

int SpoolPrinterCount(void)
{
    return (int)ARRAYSIZE(g_printers);
}

const SpoolPrinter *SpoolPrinterAt(int index)
{
    return index >= 0 && index < SpoolPrinterCount() ? &g_printers[index] : nullptr;
}

int SpoolFindPrinter(LPCWSTR name)
{
    if (!name || !*name)
        return -1;
    if (name[0] == L'\\' && name[1] == L'\\') {
        /* \\computer\printer: the computer has to be this one. */
        LPCWSTR slash = wcschr(name + 2, L'\\');
        if (!slash)
            return -1;
        WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        SIZE_T n = (SIZE_T)(slash - (name + 2));
        BOOL local = (n == 9 && !_wcsnicmp(name + 2, L"localhost", 9));
        if (!local && GetComputerNameW(computer, &size))
            local = n == wcslen(computer) && !_wcsnicmp(name + 2, computer, n);
        if (!local)
            return -1;
        name = slash + 1;
    }
    for (int i = 0; i < SpoolPrinterCount(); i++)
        if (!_wcsicmp(name, g_printers[i].Name))
            return i;
    return -1;
}

/* ------------------------------------------------------------------ the default printer */

static const WCHAR g_windows_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows";

int SpoolDefaultPrinter(void)
{
    WCHAR value[260];
    DWORD size = sizeof(value), type = 0;
    HKEY key;
    int found = -1;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, g_windows_key, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"Device", nullptr, &type, (LPBYTE)value, &size) == ERROR_SUCCESS && type == REG_SZ &&
            size >= sizeof(WCHAR)) {
            value[ARRAYSIZE(value) - 1] = 0;
            /* "name,winspool,port": the name is what comes before the first comma. */
            WCHAR *comma = wcschr(value, L',');
            if (comma)
                *comma = 0;
            found = SpoolFindPrinter(value);
        }
        RegCloseKey(key);
    }
    return found >= 0 ? found : (SpoolPrinterCount() ? 0 : -1);
}

BOOL SpoolSetDefaultPrinter(int index)
{
    const SpoolPrinter *p = SpoolPrinterAt(index);
    HKEY key;
    if (!p)
        return FALSE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_windows_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return FALSE;
    WCHAR value[320];
    _snwprintf(value, ARRAYSIZE(value), L"%ls,winspool,%ls", p->Name, p->Port);
    LSTATUS status = RegSetValueExW(key, L"Device", 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ papers and resolutions */

static const SpoolPaper g_papers[] = {
    {DMPAPER_LETTER, L"Letter", 2159, 2794},    {DMPAPER_LEGAL, L"Legal", 2159, 3556},
    {DMPAPER_TABLOID, L"Tabloid", 2794, 4318},  {DMPAPER_EXECUTIVE, L"Executive", 1842, 2667},
    {DMPAPER_STATEMENT, L"Statement", 1397, 2159}, {DMPAPER_A3, L"A3", 2970, 4200},
    {DMPAPER_A4, L"A4", 2100, 2970},            {DMPAPER_A5, L"A5", 1480, 2100},
    {DMPAPER_A6, L"A6", 1050, 1480},            {DMPAPER_B4, L"B4 (JIS)", 2570, 3640},
    {DMPAPER_B5, L"B5 (JIS)", 1820, 2570},      {DMPAPER_FOLIO, L"Folio", 2159, 3302},
    {DMPAPER_NOTE, L"Note", 2159, 2794},        {DMPAPER_ENV_10, L"Envelope #10", 1048, 2413},
    {DMPAPER_ENV_DL, L"Envelope DL", 1100, 2200}, {DMPAPER_ENV_C5, L"Envelope C5", 1620, 2290},
};

int SpoolPaperCount(void)
{
    return (int)ARRAYSIZE(g_papers);
}

const SpoolPaper *SpoolPaperAt(int index)
{
    return index >= 0 && index < SpoolPaperCount() ? &g_papers[index] : nullptr;
}

const SpoolPaper *SpoolFindPaper(WORD id)
{
    for (int i = 0; i < SpoolPaperCount(); i++)
        if (g_papers[i].Id == id)
            return &g_papers[i];
    return nullptr;
}

const SpoolPaper *SpoolFindPaperByName(LPCWSTR name)
{
    for (int i = 0; i < SpoolPaperCount(); i++)
        if (!_wcsicmp(g_papers[i].Name, name))
            return &g_papers[i];
    return nullptr;
}

/* pages are rendered in memory; the largest is about 9 million pixels at 300 DPI. */
static const int g_resolutions[] = {150, 300};

int SpoolResolutionCount(void)
{
    return (int)ARRAYSIZE(g_resolutions);
}

int SpoolResolutionAt(int index)
{
    return index >= 0 && index < SpoolResolutionCount() ? g_resolutions[index] : 0;
}

/* chooses the nearest supported resolution, mapping Windows quality constants first. */
static int distance(int a, int b)
{
    return a > b ? a - b : b - a;
}

static int nearest_resolution(int request)
{
    if (request == DMRES_DRAFT || request == DMRES_LOW)
        return g_resolutions[0];
    if (request == DMRES_MEDIUM || request == DMRES_HIGH || request <= 0)
        return g_resolutions[ARRAYSIZE(g_resolutions) - 1];
    int best = g_resolutions[0];
    for (int i = 0; i < SpoolResolutionCount(); i++)
        if (distance(request, g_resolutions[i]) < distance(request, best))
            best = g_resolutions[i];
    return best;
}

/* ------------------------------------------------------------------ device modes */

#define DEVMODE_FIELDS_SUPPORTED                                                                                   \
    (DM_ORIENTATION | DM_PAPERSIZE | DM_PAPERLENGTH | DM_PAPERWIDTH | DM_SCALE | DM_COPIES | DM_DEFAULTSOURCE |    \
     DM_PRINTQUALITY | DM_COLOR | DM_DUPLEX | DM_YRESOLUTION | DM_TTOPTION | DM_COLLATE | DM_FORMNAME |            \
     DM_ICMMETHOD | DM_ICMINTENT | DM_MEDIATYPE | DM_DITHERTYPE)

static WORD locale_paper(void)
{
    WORD paper = 0;
    DWORD value = 0;
    if (GetLocaleInfoW(0x0400, LOCALE_IPAPERSIZE | LOCALE_RETURN_NUMBER, (LPWSTR)&value, sizeof(value) / sizeof(WCHAR)))
        paper = (WORD)value;
    return paper == DMPAPER_LETTER || paper == DMPAPER_LEGAL ? paper : DMPAPER_A4;
}

static void set_paper(DEVMODEW *dm, const SpoolPaper *paper)
{
    dm->dmPaperSize = (short)paper->Id;
    dm->dmFields |= DM_PAPERSIZE | DM_FORMNAME;
    wcscpy(dm->dmFormName, paper->Name);
}

DEVMODEW *SpoolDefaultDevmode(int printer)
{
    const SpoolPrinter *p = SpoolPrinterAt(printer);
    if (!p)
        return nullptr;
    DEVMODEW *dm = SpoolAlloc(sizeof(DEVMODEW));
    if (!dm)
        return nullptr;
    _snwprintf(dm->dmDeviceName, CCHDEVICENAME, L"%ls", p->Name);
    dm->dmSpecVersion = 0x0401;
    dm->dmDriverVersion = 0x0401;
    dm->dmSize = sizeof(DEVMODEW);
    dm->dmDriverExtra = 0;
    dm->dmFields = DEVMODE_FIELDS_SUPPORTED;
    dm->dmOrientation = DMORIENT_PORTRAIT;
    dm->dmScale = 100;
    dm->dmCopies = 1;
    dm->dmDefaultSource = DMBIN_AUTO;
    dm->dmPrintQuality = 300;
    dm->dmColor = DMCOLOR_COLOR;
    dm->dmDuplex = DMDUP_SIMPLEX;
    dm->dmYResolution = 300;
    dm->dmTTOption = DMTT_BITMAP;
    dm->dmCollate = DMCOLLATE_FALSE;
    dm->dmICMMethod = DMICMMETHOD_SYSTEM;
    dm->dmICMIntent = DMICM_CONTRAST;
    dm->dmMediaType = DMMEDIA_STANDARD;
    dm->dmDitherType = DMDITHER_FINE;
    set_paper(dm, SpoolFindPaper(locale_paper()));
    return dm;
}

void SpoolMergeDevmode(int printer, DEVMODEW *dest, const DEVMODEW *src)
{
    if (!src || !dest)
        return;
    DWORD f = src->dmFields;
    /* copies only fields covered by the source's dmSize. */
    SIZE_T size = src->dmSize;
    if ((f & DM_ORIENTATION) && size >= offsetof(DEVMODEW, dmOrientation) + 2 &&
        (src->dmOrientation == DMORIENT_PORTRAIT || src->dmOrientation == DMORIENT_LANDSCAPE))
        dest->dmOrientation = src->dmOrientation;
    if ((f & DM_PAPERSIZE) && size >= offsetof(DEVMODEW, dmPaperSize) + 2) {
        const SpoolPaper *paper = SpoolFindPaper((WORD)src->dmPaperSize);
        if (paper)
            set_paper(dest, paper);
        else if (src->dmPaperSize == DMPAPER_USER) {
            dest->dmPaperSize = DMPAPER_USER;
            dest->dmFormName[0] = 0;
        }
    }
    if ((f & DM_PAPERLENGTH) && size >= offsetof(DEVMODEW, dmPaperLength) + 2 && src->dmPaperLength > 0) {
        dest->dmPaperLength = src->dmPaperLength;
        dest->dmFields |= DM_PAPERLENGTH;
    }
    if ((f & DM_PAPERWIDTH) && size >= offsetof(DEVMODEW, dmPaperWidth) + 2 && src->dmPaperWidth > 0) {
        dest->dmPaperWidth = src->dmPaperWidth;
        dest->dmFields |= DM_PAPERWIDTH;
    }
    if ((f & DM_FORMNAME) && size >= offsetof(DEVMODEW, dmFormName) + sizeof(src->dmFormName)) {
        WCHAR name[CCHFORMNAME];
        _snwprintf(name, CCHFORMNAME, L"%ls", src->dmFormName);
        const SpoolPaper *paper = SpoolFindPaperByName(name);
        if (paper)
            set_paper(dest, paper);
    }
    if ((f & DM_SCALE) && size >= offsetof(DEVMODEW, dmScale) + 2 && src->dmScale >= 10 && src->dmScale <= 400)
        dest->dmScale = src->dmScale;
    if ((f & DM_COPIES) && size >= offsetof(DEVMODEW, dmCopies) + 2 && src->dmCopies >= 1)
        dest->dmCopies = src->dmCopies;
    if ((f & DM_PRINTQUALITY) && size >= offsetof(DEVMODEW, dmPrintQuality) + 2) {
        dest->dmPrintQuality = (short)nearest_resolution(src->dmPrintQuality);
        dest->dmYResolution = dest->dmPrintQuality;
    }
    if ((f & DM_YRESOLUTION) && (f & DM_PRINTQUALITY) && size >= offsetof(DEVMODEW, dmYResolution) + 2 &&
        src->dmYResolution > 0)
        dest->dmYResolution = (short)nearest_resolution(src->dmYResolution);
    if ((f & DM_COLOR) && size >= offsetof(DEVMODEW, dmColor) + 2 &&
        (src->dmColor == DMCOLOR_COLOR || src->dmColor == DMCOLOR_MONOCHROME))
        dest->dmColor = src->dmColor;
    if ((f & DM_COLLATE) && size >= offsetof(DEVMODEW, dmCollate) + 2 &&
        (src->dmCollate == DMCOLLATE_TRUE || src->dmCollate == DMCOLLATE_FALSE))
        dest->dmCollate = src->dmCollate;
    if ((f & DM_TTOPTION) && size >= offsetof(DEVMODEW, dmTTOption) + 2 && src->dmTTOption >= DMTT_BITMAP &&
        src->dmTTOption <= DMTT_SUBDEV)
        dest->dmTTOption = src->dmTTOption;
    if ((f & DM_ICMMETHOD) && size >= offsetof(DEVMODEW, dmICMMethod) + 4)
        dest->dmICMMethod = src->dmICMMethod;
    if ((f & DM_ICMINTENT) && size >= offsetof(DEVMODEW, dmICMIntent) + 4)
        dest->dmICMIntent = src->dmICMIntent;
    if ((f & DM_MEDIATYPE) && size >= offsetof(DEVMODEW, dmMediaType) + 4)
        dest->dmMediaType = src->dmMediaType;
    if ((f & DM_DITHERTYPE) && size >= offsetof(DEVMODEW, dmDitherType) + 4)
        dest->dmDitherType = src->dmDitherType;
    (void)printer;
}

/* loads per-printer user settings from the registry on first use. */
static DEVMODEW *g_user[ARRAYSIZE(g_printers)];
static BOOL g_user_loaded[ARRAYSIZE(g_printers)];
static SRWLOCK g_user_lock = SRWLOCK_INIT;
static const WCHAR g_devmode_key[] = L"Printers\\DevModePerUser";

static DEVMODEW *read_user_devmode(int printer)
{
    HKEY key;
    DEVMODEW *result = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, g_devmode_key, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return nullptr;
    DWORD size = 0, type = 0;
    const WCHAR *name = g_printers[printer].Name;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_BINARY &&
        size >= sizeof(DEVMODEW)) {
        DEVMODEW *stored = SpoolAlloc(size);
        if (stored && RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)stored, &size) == ERROR_SUCCESS &&
            stored->dmSize == sizeof(DEVMODEW) && !_wcsicmp(stored->dmDeviceName, name)) {
            result = stored;
        } else {
            SpoolFree(stored);
        }
    }
    RegCloseKey(key);
    return result;
}

DEVMODEW *SpoolUserDevmode(int printer)
{
    DEVMODEW *dm = SpoolDefaultDevmode(printer);
    if (!dm)
        return nullptr;
    AcquireSRWLockExclusive(&g_user_lock);
    if (!g_user_loaded[printer]) {
        g_user_loaded[printer] = TRUE;
        g_user[printer] = read_user_devmode(printer);
    }
    if (g_user[printer])
        SpoolMergeDevmode(printer, dm, g_user[printer]);
    ReleaseSRWLockExclusive(&g_user_lock);
    return dm;
}

void SpoolStoreUserDevmode(int printer, const DEVMODEW *devmode)
{
    if (printer < 0 || printer >= SpoolPrinterCount() || !devmode || devmode->dmSize < sizeof(DEVMODEW))
        return;
    DEVMODEW *copy = SpoolAlloc(sizeof(DEVMODEW));
    if (!copy)
        return;
    memcpy(copy, devmode, sizeof(DEVMODEW));
    AcquireSRWLockExclusive(&g_user_lock);
    SpoolFree(g_user[printer]);
    g_user[printer] = copy;
    g_user_loaded[printer] = TRUE;
    ReleaseSRWLockExclusive(&g_user_lock);
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_devmode_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
        ERROR_SUCCESS) {
        RegSetValueExW(key, g_printers[printer].Name, 0, REG_BINARY, (const BYTE *)copy, sizeof(DEVMODEW));
        RegCloseKey(key);
    }
}

/* ------------------------------------------------------------------ ANSI device modes */

DEVMODEA *SpoolDevmodeToAnsi(const DEVMODEW *w)
{
    SIZE_T extra = w->dmDriverExtra;
    DEVMODEA *a = SpoolAlloc(sizeof(DEVMODEA) + extra);
    if (!a)
        return nullptr;
    WideCharToMultiByte(CP_ACP, 0, w->dmDeviceName, -1, (LPSTR)a->dmDeviceName, CCHDEVICENAME, nullptr, nullptr);
    memcpy(&a->dmSpecVersion, &w->dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
    WideCharToMultiByte(CP_ACP, 0, w->dmFormName, -1, (LPSTR)a->dmFormName, CCHFORMNAME, nullptr, nullptr);
    memcpy(&a->dmLogPixels, &w->dmLogPixels,
           offsetof(DEVMODEA, dmPanningHeight) + sizeof(a->dmPanningHeight) - offsetof(DEVMODEA, dmLogPixels));
    a->dmSize = sizeof(DEVMODEA);
    a->dmDriverExtra = (WORD)extra;
    if (extra)
        memcpy((BYTE *)a + sizeof(DEVMODEA), (const BYTE *)w + w->dmSize, extra);
    return a;
}

DEVMODEW *SpoolDevmodeFromAnsi(const DEVMODEA *a)
{
    SIZE_T extra = a->dmDriverExtra;
    DEVMODEW *w = SpoolAlloc(sizeof(DEVMODEW) + extra);
    if (!w)
        return nullptr;
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)a->dmDeviceName, -1, w->dmDeviceName, CCHDEVICENAME);
    memcpy(&w->dmSpecVersion, &a->dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)a->dmFormName, -1, w->dmFormName, CCHFORMNAME);
    memcpy(&w->dmLogPixels, &a->dmLogPixels,
           offsetof(DEVMODEA, dmPanningHeight) + sizeof(a->dmPanningHeight) - offsetof(DEVMODEA, dmLogPixels));
    w->dmSize = sizeof(DEVMODEW);
    w->dmDriverExtra = (WORD)extra;
    if (extra)
        memcpy((BYTE *)w + sizeof(DEVMODEW), (const BYTE *)a + a->dmSize, extra);
    return w;
}
