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

/*
 * after the mode checks windows opens the device when no handle is given, then
 * validates the handle; with no printer that leaves ERROR_INVALID_HANDLE.
 */
static LONG SpoolDocumentProperties(const void *output, DWORD mode)
{
    if ((mode & (DM_NOPERMISSION | DM_PROMPT)) == DM_NOPERMISSION || ((mode & DM_OUT_BUFFER) && !output)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    SetLastError(ERROR_INVALID_HANDLE);
    return -1;
}

DECLSPEC_EXPORT LONG WINAPI DocumentPropertiesW(HWND window, HANDLE printer, LPWSTR device, PDEVMODEW output,
                                                      PDEVMODEW input, DWORD mode)
{
    return SpoolDocumentProperties(output, mode);
}

DECLSPEC_EXPORT LONG WINAPI DocumentPropertiesA(HWND window, HANDLE printer, LPSTR device, PDEVMODEA output,
                                                      PDEVMODEA input, DWORD mode)
{
    return SpoolDocumentProperties(output, mode);
}

/* shows the driver's advanced page; returns 1 when it was confirmed. */
DECLSPEC_EXPORT LONG WINAPI AdvancedDocumentPropertiesW(HWND window, HANDLE printer, LPWSTR device,
                                                              PDEVMODEW output, PDEVMODEW input)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
}

DECLSPEC_EXPORT LONG WINAPI AdvancedDocumentPropertiesA(HWND window, HANDLE printer, LPSTR device,
                                                              PDEVMODEA output, PDEVMODEA input)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
}

DECLSPEC_EXPORT LONG WINAPI AdvancedSetupDialog(HWND window, HANDLE instance, LPDEVMODEA input,
                                                      LPDEVMODEA output)
{
    SetLastError(ERROR_INVALID_PRINTER_NAME);
    return -1;
}

/* ------------------------------------------------------------------ device capabilities */

DECLSPEC_EXPORT int WINAPI DeviceCapabilitiesW(LPCWSTR device, LPCWSTR port, WORD capability, LPWSTR output,
                                                     const DEVMODEW *devmode)
{
    if (devmode && !SpoolValidDevmodeW((PDEVMODEW)devmode))
        return -1;
    SetLastError(ERROR_INVALID_PRINTER_NAME);
    return -1;
}

DECLSPEC_EXPORT int WINAPI DeviceCapabilitiesA(LPCSTR device, LPCSTR port, WORD capability, LPSTR output,
                                                     const DEVMODEA *devmode)
{
    if (devmode && !SpoolValidDevmodeA((PDEVMODEA)devmode))
        return -1;
    SetLastError(ERROR_INVALID_PRINTER_NAME);
    return -1;
}

/* the 16-bit era entry points: ExtDeviceMode opens the device by name first. */
DECLSPEC_EXPORT LONG WINAPI ExtDeviceMode(HWND window, HANDLE instance, LPDEVMODEA output, LPSTR device,
                                                LPSTR port, LPDEVMODEA input, LPSTR profile, DWORD mode)
{
    SetLastError(ERROR_INVALID_PRINTER_NAME);
    return -1;
}

DECLSPEC_EXPORT void WINAPI DeviceMode(HWND window, HANDLE instance, LPSTR device, LPSTR port)
{
}
