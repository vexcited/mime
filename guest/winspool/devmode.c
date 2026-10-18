/*
 * device modes: validation, document properties and device capabilities.
 * without printer drivers no device can be queried.
 */
#include "winspool_private.h"
#include <stddef.h>

/* the device mode size each field needs, the end offset of its member. */
#define FIELD_END(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))
#define DEVMODE_FIELDS(type)                                                                                      \
    {                                                                                                             \
        {DM_PANNINGHEIGHT, FIELD_END(type, dmPanningHeight)},                                                     \
        {DM_PANNINGWIDTH, FIELD_END(type, dmPanningWidth)},                                                       \
        {DM_DITHERTYPE, FIELD_END(type, dmDitherType)},                                                           \
        {DM_MEDIATYPE, FIELD_END(type, dmMediaType)},                                                             \
        {DM_ICMINTENT, FIELD_END(type, dmICMIntent)},                                                             \
        {DM_ICMMETHOD, FIELD_END(type, dmICMMethod)},                                                             \
        {DM_DISPLAYFREQUENCY, FIELD_END(type, dmDisplayFrequency)},                                               \
        {DM_DISPLAYFLAGS, FIELD_END(type, dmDisplayFlags)},                                                       \
        {DM_NUP, FIELD_END(type, dmNup)},                                                                         \
        {DM_PELSHEIGHT, FIELD_END(type, dmPelsHeight)},                                                           \
        {DM_PELSWIDTH, FIELD_END(type, dmPelsWidth)},                                                             \
        {DM_BITSPERPEL, FIELD_END(type, dmBitsPerPel)},                                                           \
        {DM_LOGPIXELS, FIELD_END(type, dmLogPixels)},                                                             \
        {DM_FORMNAME, FIELD_END(type, dmFormName)},                                                               \
        {DM_COLLATE, FIELD_END(type, dmCollate)},                                                                 \
        {DM_TTOPTION, FIELD_END(type, dmTTOption)},                                                               \
        {DM_YRESOLUTION, FIELD_END(type, dmYResolution)},                                                         \
        {DM_DUPLEX, FIELD_END(type, dmDuplex)},                                                                   \
        {DM_COLOR, FIELD_END(type, dmColor)},                                                                     \
        {DM_DISPLAYFIXEDOUTPUT, FIELD_END(type, dmDisplayFixedOutput)},                                           \
        {DM_DISPLAYORIENTATION, FIELD_END(type, dmDisplayOrientation)},                                           \
        {DM_POSITION, FIELD_END(type, dmPosition)},                                                               \
        {DM_PRINTQUALITY, FIELD_END(type, dmPrintQuality)},                                                       \
        {DM_DEFAULTSOURCE, FIELD_END(type, dmDefaultSource)},                                                     \
        {DM_COPIES, FIELD_END(type, dmCopies)},                                                                   \
        {DM_SCALE, FIELD_END(type, dmScale)},                                                                     \
        {DM_PAPERWIDTH, FIELD_END(type, dmPaperWidth)},                                                           \
        {DM_PAPERLENGTH, FIELD_END(type, dmPaperLength)},                                                         \
        {DM_PAPERSIZE, FIELD_END(type, dmPaperSize)},                                                             \
        {DM_ORIENTATION, FIELD_END(type, dmOrientation)},                                                         \
    }

typedef struct {
    DWORD field;
    SIZE_T end;
} SpoolDevmodeField;

static const SpoolDevmodeField g_fields_w[] = DEVMODE_FIELDS(DEVMODEW);
static const SpoolDevmodeField g_fields_a[] = DEVMODE_FIELDS(DEVMODEA);

/* checks the header, then that every field dmFields announces fits in dmSize. */
static BOOL SpoolCheckFields(const SpoolDevmodeField *fields, SIZE_T count, DWORD present, WORD size, WORD extra,
                             SIZE_T minimum)
{
    if (size < minimum)
        return FALSE;
    if (extra && (size & 3))
        return FALSE;
    for (SIZE_T i = 0; i < count; i++) {
        if ((present & fields[i].field) && size < fields[i].end)
            return FALSE;
    }
    return TRUE;
}

/* validates a unicode device mode and terminates its names, as the spooler client does. */
static BOOL SpoolValidDevmodeW(PDEVMODEW dm)
{
    if (!dm || dm == (PDEVMODEW)(LONG_PTR)-1 ||
        !SpoolCheckFields(g_fields_w, ARRAYSIZE(g_fields_w), dm->dmFields, dm->dmSize, dm->dmDriverExtra,
                          offsetof(DEVMODEW, dmFields) + sizeof(DWORD))) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (wcsnlen(dm->dmDeviceName, CCHDEVICENAME) == CCHDEVICENAME)
        dm->dmDeviceName[CCHDEVICENAME - 1] = 0;
    if ((dm->dmFields & DM_FORMNAME) && wcsnlen(dm->dmFormName, CCHFORMNAME) == CCHFORMNAME)
        dm->dmFormName[CCHFORMNAME - 1] = 0;
    return TRUE;
}

static BOOL SpoolValidDevmodeA(PDEVMODEA dm)
{
    if (!dm || dm == (PDEVMODEA)(LONG_PTR)-1 ||
        !SpoolCheckFields(g_fields_a, ARRAYSIZE(g_fields_a), dm->dmFields, dm->dmSize, dm->dmDriverExtra,
                          offsetof(DEVMODEA, dmFields) + sizeof(DWORD))) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (strnlen((PCSTR)dm->dmDeviceName, CCHDEVICENAME) == CCHDEVICENAME)
        dm->dmDeviceName[CCHDEVICENAME - 1] = 0;
    if ((dm->dmFields & DM_FORMNAME) && strnlen((PCSTR)dm->dmFormName, CCHFORMNAME) == CCHFORMNAME)
        dm->dmFormName[CCHFORMNAME - 1] = 0;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI IsValidDevmodeW(PDEVMODEW devmode, SIZE_T size)
{
    if (!devmode || size < offsetof(DEVMODEW, dmFields) + sizeof(DWORD) ||
        size < (SIZE_T)devmode->dmSize + devmode->dmDriverExtra)
        return SpoolFail(ERROR_INVALID_DATA);
    return SpoolValidDevmodeW(devmode);
}

DECLSPEC_EXPORT BOOL WINAPI IsValidDevmodeA(PDEVMODEA devmode, SIZE_T size)
{
    if (!devmode || size < offsetof(DEVMODEA, dmFields) + sizeof(DWORD) ||
        size < (SIZE_T)devmode->dmSize + devmode->dmDriverExtra)
        return SpoolFail(ERROR_INVALID_DATA);
    return SpoolValidDevmodeA(devmode);
}

/* ------------------------------------------------------------------ document properties */

/* the printer a call names: by its handle, else by the device name which is opened for the call. */
static int SpoolDeviceOf(HANDLE printer, LPCWSTR device)
{
    if (printer) {
        SpoolHandle *h = SpoolLookup(printer);
        if (!h)
            return -1;
        if (h->printer < 0) {
            SetLastError(ERROR_INVALID_HANDLE);
            return -1;
        }
        return h->printer;
    }
    int index = SpoolFindPrinter(device);
    if (index < 0)
        SetLastError(ERROR_INVALID_HANDLE);
    return index;
}

/*
 * after the mode checks windows opens the device when no handle is given, then validates the handle. a
 * mode of 0 asks for the size of the output; DM_IN_BUFFER merges the input into the settings, DM_PROMPT
 * lets the user change them and answers IDOK or IDCANCEL, DM_OUT_BUFFER writes them, DM_UPDATE keeps them.
 */
static LONG SpoolDocumentProperties(HWND window, HANDLE printer, LPCWSTR device, PDEVMODEW output,
                                    const DEVMODEW *input, DWORD mode)
{
    if ((mode & (DM_NOPERMISSION | DM_PROMPT)) == DM_NOPERMISSION || ((mode & DM_OUT_BUFFER) && !output)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    int index = SpoolDeviceOf(printer, device);
    if (index < 0)
        return -1;
    if (!mode)
        return (LONG)sizeof(DEVMODEW);
    DEVMODEW *dm = SpoolUserDevmode(index);
    if (!dm) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    LONG result = IDOK;
    if ((mode & DM_IN_BUFFER) && input && SpoolValidDevmodeW((PDEVMODEW)input))
        SpoolMergeDevmode(index, dm, input);
    if (mode & DM_IN_PROMPT) {
        result = (LONG)SpoolPreferences(window, index, dm);
        if (result != IDOK) {
            SpoolFree(dm);
            return result;
        }
    }
    if (mode & DM_OUT_BUFFER)
        memcpy(output, dm, sizeof(DEVMODEW));
    if (mode & DM_UPDATE)
        SpoolStoreUserDevmode(index, dm);
    SpoolFree(dm);
    return result;
}

DECLSPEC_EXPORT LONG WINAPI DocumentPropertiesW(HWND window, HANDLE printer, LPWSTR device, PDEVMODEW output,
                                                      PDEVMODEW input, DWORD mode)
{
    return SpoolDocumentProperties(window, printer, device, output, input, mode);
}

/* the ANSI form works on ANSI device modes, converted at the edges; the size asked for is that of an ANSI one. */
DECLSPEC_EXPORT LONG WINAPI DocumentPropertiesA(HWND window, HANDLE printer, LPSTR device, PDEVMODEA output,
                                                      PDEVMODEA input, DWORD mode)
{
    if ((mode & (DM_NOPERMISSION | DM_PROMPT)) == DM_NOPERMISSION || ((mode & DM_OUT_BUFFER) && !output)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    PWSTR wide_device = SpoolToWide(device);
    DEVMODEW *wide_input = (mode & DM_IN_BUFFER) && input && SpoolValidDevmodeA(input) ? SpoolDevmodeFromAnsi(input) : nullptr;
    DEVMODEW wide_output;
    LONG result = SpoolDocumentProperties(window, printer, wide_device, (mode & DM_OUT_BUFFER) ? &wide_output : nullptr,
                                          wide_input, mode);
    DWORD error = GetLastError();
    if (!mode && result == (LONG)sizeof(DEVMODEW))
        result = (LONG)sizeof(DEVMODEA);
    if (result > 0 && (mode & DM_OUT_BUFFER)) {
        DEVMODEA *a = SpoolDevmodeToAnsi(&wide_output);
        if (a) {
            memcpy(output, a, sizeof(DEVMODEA));
            SpoolFree(a);
        }
    }
    SpoolFree(wide_device);
    SpoolFree(wide_input);
    SetLastError(error);
    return result;
}

/* the page of the driver's own options: this driver has none beyond the preferences. */
DECLSPEC_EXPORT LONG WINAPI AdvancedDocumentPropertiesW(HWND window, HANDLE printer, LPWSTR device,
                                                              PDEVMODEW output, PDEVMODEW input)
{
    int index = SpoolDeviceOf(printer, device);
    if (index < 0)
        return 0;
    DEVMODEW *dm = SpoolUserDevmode(index);
    if (!dm) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    if (input && SpoolValidDevmodeW(input))
        SpoolMergeDevmode(index, dm, input);
    LONG result = SpoolPreferences(window, index, dm) == IDOK ? 1 : 0;
    if (result && output)
        memcpy(output, dm, sizeof(DEVMODEW));
    SpoolFree(dm);
    return result;
}

DECLSPEC_EXPORT LONG WINAPI AdvancedDocumentPropertiesA(HWND window, HANDLE printer, LPSTR device,
                                                              PDEVMODEA output, PDEVMODEA input)
{
    PWSTR wide_device = SpoolToWide(device);
    DEVMODEW *wide_input = input && SpoolValidDevmodeA(input) ? SpoolDevmodeFromAnsi(input) : nullptr;
    DEVMODEW wide_output;
    LONG result = AdvancedDocumentPropertiesW(window, printer, wide_device, output ? &wide_output : nullptr, wide_input);
    if (result && output) {
        DEVMODEA *a = SpoolDevmodeToAnsi(&wide_output);
        if (a) {
            memcpy(output, a, sizeof(DEVMODEA));
            SpoolFree(a);
        }
    }
    SpoolFree(wide_device);
    SpoolFree(wide_input);
    return result;
}

DECLSPEC_EXPORT LONG WINAPI AdvancedSetupDialog(HWND window, HANDLE instance, LPDEVMODEA input,
                                                      LPDEVMODEA output)
{
    SetLastError(ERROR_INVALID_PRINTER_NAME);
    return -1;
}

/* ------------------------------------------------------------------ device capabilities */

/* an array of text fields of a fixed width, in the character set of the caller. */
static int SpoolCapNames(const WCHAR **names, int count, void *output, int width, BOOL ansi)
{
    if (output) {
        for (int i = 0; i < count; i++) {
            if (ansi) {
                char *out = (char *)output + (SIZE_T)i * width;
                memset(out, 0, (SIZE_T)width);
                WideCharToMultiByte(CP_ACP, 0, names[i], -1, out, width, nullptr, nullptr);
                out[width - 1] = 0;
            } else {
                WCHAR *out = (WCHAR *)output + (SIZE_T)i * width;
                memset(out, 0, (SIZE_T)width * sizeof(WCHAR));
                for (int c = 0; c < width - 1 && names[i][c]; c++)
                    out[c] = names[i][c];
            }
        }
    }
    return count;
}

/* the answers of DeviceCapabilities, wide: the callers that want ANSI text pass ansi. */
static int SpoolCapabilities(int index, WORD capability, void *output, const DEVMODEW *devmode, BOOL ansi)
{
    (void)devmode;
    int paper_count = SpoolPaperCount();
    switch (capability) {
    case DC_FIELDS:
        return (int)(DM_ORIENTATION | DM_PAPERSIZE | DM_PAPERLENGTH | DM_PAPERWIDTH | DM_SCALE | DM_COPIES |
                     DM_DEFAULTSOURCE | DM_PRINTQUALITY | DM_COLOR | DM_DUPLEX | DM_YRESOLUTION | DM_TTOPTION |
                     DM_COLLATE | DM_FORMNAME | DM_ICMMETHOD | DM_ICMINTENT | DM_MEDIATYPE | DM_DITHERTYPE);
    case DC_PAPERS:
        if (output)
            for (int i = 0; i < paper_count; i++)
                ((WORD *)output)[i] = SpoolPaperAt(i)->Id;
        return paper_count;
    case DC_PAPERSIZE:
        if (output)
            for (int i = 0; i < paper_count; i++) {
                ((POINT *)output)[i].x = SpoolPaperAt(i)->Width;
                ((POINT *)output)[i].y = SpoolPaperAt(i)->Height;
            }
        return paper_count;
    case DC_PAPERNAMES: {
        const WCHAR *names[64];
        for (int i = 0; i < paper_count && i < 64; i++)
            names[i] = SpoolPaperAt(i)->Name;
        return SpoolCapNames(names, paper_count, output, 64, ansi);
    }
    case DC_MINEXTENT:
    case DC_MAXEXTENT: {
        int w = capability == DC_MINEXTENT ? 0x7FFF : 0, h = w;
        for (int i = 0; i < paper_count; i++) {
            const SpoolPaper *p = SpoolPaperAt(i);
            if (capability == DC_MINEXTENT) {
                if (p->Width < w)
                    w = p->Width;
                if (p->Height < h)
                    h = p->Height;
            } else {
                if (p->Width > w)
                    w = p->Width;
                if (p->Height > h)
                    h = p->Height;
            }
        }
        return (int)MAKELONG(w, h);
    }
    case DC_BINS:
        if (output)
            ((WORD *)output)[0] = DMBIN_AUTO;
        return 1;
    case DC_BINNAMES: {
        const WCHAR *names[1] = {L"Automatically Select"};
        return SpoolCapNames(names, 1, output, 24, ansi);
    }
    case DC_DUPLEX:
        return 0;
    case DC_SIZE:
        return ansi ? (int)sizeof(DEVMODEA) : (int)sizeof(DEVMODEW);
    case DC_EXTRA:
        return 0;
    case DC_VERSION:
        return 0x0401;
    case DC_DRIVER:
        return 0x0401;
    case DC_ENUMRESOLUTIONS:
        if (output)
            for (int i = 0; i < SpoolResolutionCount(); i++) {
                ((LONG *)output)[2 * i] = SpoolResolutionAt(i);
                ((LONG *)output)[2 * i + 1] = SpoolResolutionAt(i);
            }
        return SpoolResolutionCount();
    case DC_FILEDEPENDENCIES:
        return 0;
    case DC_TRUETYPE:
        return DCTT_BITMAP | DCTT_DOWNLOAD;
    case DC_ORIENTATION:
        return 90;
    case DC_COPIES:
        return 1;
    case DC_COLLATE:
        return 0;
    case DC_COLORDEVICE:
        return 1;
    case DC_NUP:
        if (output)
            ((DWORD *)output)[0] = 1;
        return 1;
    case DC_MANUFACTURER:
    case DC_MODEL: {
        const SpoolPrinter *pr = SpoolPrinterAt(index);
        const WCHAR *names[1] = {capability == DC_MANUFACTURER ? pr->Manufacturer : pr->Model};
        return SpoolCapNames(names, 1, output, 32, ansi) ? (int)wcslen(names[0]) + 1 : 0;
    }
    case DC_STAPLE:
        return 0;
    case DC_PRINTERMEM:
    case DC_PRINTRATE:
    case DC_PRINTRATEUNIT:
    case DC_PRINTRATEPPM:
    case DC_PERSONALITY:
    case DC_MEDIAREADY:
    case DC_MEDIATYPENAMES:
    case DC_MEDIATYPES:
        return -1;
    }
    return -1;
}

DECLSPEC_EXPORT int WINAPI DeviceCapabilitiesW(LPCWSTR device, LPCWSTR port, WORD capability, LPWSTR output,
                                                     const DEVMODEW *devmode)
{
    if (devmode && !SpoolValidDevmodeW((PDEVMODEW)devmode))
        return -1;
    int index = SpoolFindPrinter(device);
    if (index < 0) {
        SetLastError(ERROR_INVALID_PRINTER_NAME);
        return -1;
    }
    return SpoolCapabilities(index, capability, output, devmode, FALSE);
}

DECLSPEC_EXPORT int WINAPI DeviceCapabilitiesA(LPCSTR device, LPCSTR port, WORD capability, LPSTR output,
                                                     const DEVMODEA *devmode)
{
    if (devmode && !SpoolValidDevmodeA((PDEVMODEA)devmode))
        return -1;
    PWSTR wide = SpoolToWide(device);
    int index = SpoolFindPrinter(wide);
    SpoolFree(wide);
    if (index < 0) {
        SetLastError(ERROR_INVALID_PRINTER_NAME);
        return -1;
    }
    return SpoolCapabilities(index, capability, output, nullptr, TRUE);
}

/* the 16-bit era entry points: ExtDeviceMode opens the device by name first. */
DECLSPEC_EXPORT LONG WINAPI ExtDeviceMode(HWND window, HANDLE instance, LPDEVMODEA output, LPSTR device,
                                                LPSTR port, LPDEVMODEA input, LPSTR profile, DWORD mode)
{
    return DocumentPropertiesA(window, nullptr, device, output, input, mode);
}

DECLSPEC_EXPORT void WINAPI DeviceMode(HWND window, HANDLE instance, LPSTR device, LPSTR port)
{
}
