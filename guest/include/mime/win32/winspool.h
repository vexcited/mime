/*
 * print spooler api of winspool.drv: printer handles, enumeration, jobs,
 * forms, drivers and device modes.
 */
#ifndef MIME_WIN32_WINSPOOL_H
#define MIME_WIN32_WINSPOOL_H

#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>

#ifdef MIME_BUILD_WINSPOOL
#define WINSPOOLAPI __declspec(dllexport)
#else
#define WINSPOOLAPI DECLSPEC_IMPORT
#endif

/* ------------------------------------------------------------------ device modes */

#ifndef DM_ORIENTATION
#define CCHFORMNAME 32
#ifndef CCHDEVICENAME
#define CCHDEVICENAME 32
#endif

#define DM_ORIENTATION 0x00000001L
#define DM_PAPERSIZE 0x00000002L
#define DM_PAPERLENGTH 0x00000004L
#define DM_PAPERWIDTH 0x00000008L
#define DM_SCALE 0x00000010L
#define DM_POSITION 0x00000020L
#define DM_NUP 0x00000040L
#define DM_DISPLAYORIENTATION 0x00000080L
#define DM_COPIES 0x00000100L
#define DM_DEFAULTSOURCE 0x00000200L
#define DM_PRINTQUALITY 0x00000400L
#define DM_COLOR 0x00000800L
#define DM_DUPLEX 0x00001000L
#define DM_YRESOLUTION 0x00002000L
#define DM_TTOPTION 0x00004000L
#define DM_COLLATE 0x00008000L
#define DM_FORMNAME 0x00010000L
#define DM_LOGPIXELS 0x00020000L
#define DM_BITSPERPEL 0x00040000L
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#define DM_DISPLAYFLAGS 0x00200000L
#define DM_DISPLAYFREQUENCY 0x00400000L
#define DM_ICMMETHOD 0x00800000L
#define DM_ICMINTENT 0x01000000L
#define DM_MEDIATYPE 0x02000000L
#define DM_DITHERTYPE 0x04000000L
#define DM_PANNINGWIDTH 0x08000000L
#define DM_PANNINGHEIGHT 0x10000000L
#define DM_DISPLAYFIXEDOUTPUT 0x20000000L

#define DM_SPECVERSION 0x0401

typedef struct _devicemodeA {
    BYTE dmDeviceName[CCHDEVICENAME];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    union {
        struct {
            short dmOrientation;
            short dmPaperSize;
            short dmPaperLength;
            short dmPaperWidth;
            short dmScale;
            short dmCopies;
            short dmDefaultSource;
            short dmPrintQuality;
        };
        struct {
            POINTL dmPosition;
            DWORD dmDisplayOrientation;
            DWORD dmDisplayFixedOutput;
        };
    };
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    BYTE dmFormName[CCHFORMNAME];
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    union {
        DWORD dmDisplayFlags;
        DWORD dmNup;
    };
    DWORD dmDisplayFrequency;
    DWORD dmICMMethod;
    DWORD dmICMIntent;
    DWORD dmMediaType;
    DWORD dmDitherType;
    DWORD dmReserved1;
    DWORD dmReserved2;
    DWORD dmPanningWidth;
    DWORD dmPanningHeight;
} DEVMODEA, *PDEVMODEA, *NPDEVMODEA, *LPDEVMODEA;

typedef struct _devicemodeW {
    WCHAR dmDeviceName[CCHDEVICENAME];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    union {
        struct {
            short dmOrientation;
            short dmPaperSize;
            short dmPaperLength;
            short dmPaperWidth;
            short dmScale;
            short dmCopies;
            short dmDefaultSource;
            short dmPrintQuality;
        };
        struct {
            POINTL dmPosition;
            DWORD dmDisplayOrientation;
            DWORD dmDisplayFixedOutput;
        };
    };
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    WCHAR dmFormName[CCHFORMNAME];
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    union {
        DWORD dmDisplayFlags;
        DWORD dmNup;
    };
    DWORD dmDisplayFrequency;
    DWORD dmICMMethod;
    DWORD dmICMIntent;
    DWORD dmMediaType;
    DWORD dmDitherType;
    DWORD dmReserved1;
    DWORD dmReserved2;
    DWORD dmPanningWidth;
    DWORD dmPanningHeight;
} DEVMODEW, *PDEVMODEW, *NPDEVMODEW, *LPDEVMODEW;
#endif

/* DocumentProperties modes. */
#define DM_UPDATE 1
#define DM_COPY 2
#define DM_PROMPT 4
#define DM_MODIFY 8
#define DM_IN_BUFFER DM_MODIFY
#define DM_IN_PROMPT DM_PROMPT
#define DM_OUT_BUFFER DM_COPY
#define DM_OUT_DEFAULT DM_UPDATE
#define DM_NOPERMISSION 0x40000000

/* DeviceCapabilities queries. */
#define DC_FIELDS 1
#define DC_PAPERS 2
#define DC_PAPERSIZE 3
#define DC_MINEXTENT 4
#define DC_MAXEXTENT 5
#define DC_BINS 6
#define DC_DUPLEX 7
#define DC_SIZE 8
#define DC_EXTRA 9
#define DC_VERSION 10
#define DC_DRIVER 11
#define DC_BINNAMES 12
#define DC_ENUMRESOLUTIONS 13
#define DC_FILEDEPENDENCIES 14
#define DC_TRUETYPE 15
#define DC_PAPERNAMES 16
#define DC_ORIENTATION 17
#define DC_COPIES 18

/* ------------------------------------------------------------------ constants */

#define PRINTER_ENUM_DEFAULT 0x00000001
#define PRINTER_ENUM_LOCAL 0x00000002
#define PRINTER_ENUM_CONNECTIONS 0x00000004
#define PRINTER_ENUM_FAVORITE 0x00000004
#define PRINTER_ENUM_NAME 0x00000008
#define PRINTER_ENUM_REMOTE 0x00000010
#define PRINTER_ENUM_SHARED 0x00000020
#define PRINTER_ENUM_NETWORK 0x00000040
#define PRINTER_ENUM_EXPAND 0x00004000
#define PRINTER_ENUM_CONTAINER 0x00008000
#define PRINTER_ENUM_ICONMASK 0x00ff0000
#define PRINTER_ENUM_HIDE 0x01000000
#define PRINTER_ENUM_CATEGORY_ALL 0x02000000
#define PRINTER_ENUM_CATEGORY_3D 0x04000000

#define SERVER_ACCESS_ADMINISTER 0x00000001
#define SERVER_ACCESS_ENUMERATE 0x00000002
#define PRINTER_ACCESS_ADMINISTER 0x00000004
#define PRINTER_ACCESS_USE 0x00000008
#define JOB_ACCESS_ADMINISTER 0x00000010
#define JOB_ACCESS_READ 0x00000020
#define PRINTER_ACCESS_MANAGE_LIMITED 0x00000040
#define PRINTER_ALL_ACCESS 0x000F000C
#define SERVER_ALL_ACCESS 0x000F0003

#define PRINTER_CHANGE_ALL 0x7777FFFF

#define PRINTER_OPTION_NO_CACHE 2
#define PRINTER_OPTION_CACHE 4
#define PRINTER_OPTION_CLIENT_CHANGE 8
#define PRINTER_OPTION_NO_CLIENT_DATA 16

#define PRINT_EXECUTION_CONTEXT_APPLICATION 0
#define PRINT_EXECUTION_CONTEXT_SPOOLER_SERVICE 1
#define PRINT_EXECUTION_CONTEXT_SPOOLER_ISOLATION_HOST 2
#define PRINT_EXECUTION_CONTEXT_FILTER_PIPELINE 3
#define PRINT_EXECUTION_CONTEXT_WOW64 4
#define PRINT_EXECUTION_DATA_LEVEL_1 1

#ifndef ERROR_UNKNOWN_PORT
#define ERROR_UNKNOWN_PORT 1796L
#endif
#ifndef ERROR_UNKNOWN_PRINTER_DRIVER
#define ERROR_UNKNOWN_PRINTER_DRIVER 1797L
#endif
#ifndef ERROR_UNKNOWN_PRINTPROCESSOR
#define ERROR_UNKNOWN_PRINTPROCESSOR 1798L
#endif
#ifndef ERROR_INVALID_ENVIRONMENT
#define ERROR_INVALID_ENVIRONMENT 1805L
#endif
#ifndef ERROR_INVALID_FORM_NAME
#define ERROR_INVALID_FORM_NAME 1902L
#endif
#ifndef ERROR_UNKNOWN_PRINT_MONITOR
#define ERROR_UNKNOWN_PRINT_MONITOR 3000L
#endif

/* ------------------------------------------------------------------ structures */

typedef struct _PRINTER_DEFAULTSA {
    LPSTR pDatatype;
    LPDEVMODEA pDevMode;
    ACCESS_MASK DesiredAccess;
} PRINTER_DEFAULTSA, *PPRINTER_DEFAULTSA, *LPPRINTER_DEFAULTSA;

typedef struct _PRINTER_DEFAULTSW {
    LPWSTR pDatatype;
    LPDEVMODEW pDevMode;
    ACCESS_MASK DesiredAccess;
} PRINTER_DEFAULTSW, *PPRINTER_DEFAULTSW, *LPPRINTER_DEFAULTSW;

typedef struct _PRINTER_OPTIONSA {
    UINT cbSize;
    DWORD dwFlags;
} PRINTER_OPTIONSA, *PPRINTER_OPTIONSA, *LPPRINTER_OPTIONSA;

typedef struct _PRINTER_OPTIONSW {
    UINT cbSize;
    DWORD dwFlags;
} PRINTER_OPTIONSW, *PPRINTER_OPTIONSW, *LPPRINTER_OPTIONSW;

typedef struct _PRINTER_INFO_1W {
    DWORD Flags;
    LPWSTR pDescription;
    LPWSTR pName;
    LPWSTR pComment;
} PRINTER_INFO_1W, *PPRINTER_INFO_1W, *LPPRINTER_INFO_1W;

typedef struct _PRINTER_INFO_2W {
    LPWSTR pServerName;
    LPWSTR pPrinterName;
    LPWSTR pShareName;
    LPWSTR pPortName;
    LPWSTR pDriverName;
    LPWSTR pComment;
    LPWSTR pLocation;
    LPDEVMODEW pDevMode;
    LPWSTR pSepFile;
    LPWSTR pPrintProcessor;
    LPWSTR pDatatype;
    LPWSTR pParameters;
    PSECURITY_DESCRIPTOR pSecurityDescriptor;
    DWORD Attributes;
    DWORD Priority;
    DWORD DefaultPriority;
    DWORD StartTime;
    DWORD UntilTime;
    DWORD Status;
    DWORD cJobs;
    DWORD AveragePPM;
} PRINTER_INFO_2W, *PPRINTER_INFO_2W, *LPPRINTER_INFO_2W;

typedef struct _PRINTER_INFO_4W {
    LPWSTR pPrinterName;
    LPWSTR pServerName;
    DWORD Attributes;
} PRINTER_INFO_4W, *PPRINTER_INFO_4W, *LPPRINTER_INFO_4W;

typedef struct _DOC_INFO_1A {
    LPSTR pDocName;
    LPSTR pOutputFile;
    LPSTR pDatatype;
} DOC_INFO_1A, *PDOC_INFO_1A, *LPDOC_INFO_1A;

typedef struct _DOC_INFO_1W {
    LPWSTR pDocName;
    LPWSTR pOutputFile;
    LPWSTR pDatatype;
} DOC_INFO_1W, *PDOC_INFO_1W, *LPDOC_INFO_1W;

typedef struct _FORM_INFO_1W {
    DWORD Flags;
    LPWSTR pName;
    SIZEL Size;
    RECTL ImageableArea;
} FORM_INFO_1W, *PFORM_INFO_1W, *LPFORM_INFO_1W;

typedef struct _DRIVER_DIRECTORY_INFO_1W {
    LPWSTR pDirectory;
} DRIVER_DIRECTORY_INFO_1W;

typedef struct _PRINTER_NOTIFY_OPTIONS_TYPE {
    WORD Type;
    WORD Reserved0;
    DWORD Reserved1;
    DWORD Reserved2;
    DWORD Count;
    PWORD pFields;
} PRINTER_NOTIFY_OPTIONS_TYPE, *PPRINTER_NOTIFY_OPTIONS_TYPE;

typedef struct _PRINTER_NOTIFY_OPTIONS {
    DWORD Version;
    DWORD Flags;
    DWORD Count;
    PPRINTER_NOTIFY_OPTIONS_TYPE pTypes;
} PRINTER_NOTIFY_OPTIONS, *PPRINTER_NOTIFY_OPTIONS;

typedef struct _PRINTER_NOTIFY_INFO {
    DWORD Version;
    DWORD Flags;
    DWORD Count;
    /* followed by Count PRINTER_NOTIFY_INFO_DATA entries. */
} PRINTER_NOTIFY_INFO, *PPRINTER_NOTIFY_INFO;

typedef struct _PRINT_EXECUTION_DATA {
    DWORD context;
    DWORD clientAppPID;
} PRINT_EXECUTION_DATA;

/* ------------------------------------------------------------------ functions */

WINSPOOLAPI BOOL WINAPI EnumPrintersA(DWORD flags, LPSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                      LPDWORD needed, LPDWORD returned);
WINSPOOLAPI BOOL WINAPI EnumPrintersW(DWORD flags, LPWSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                      LPDWORD needed, LPDWORD returned);
WINSPOOLAPI BOOL WINAPI OpenPrinterA(LPSTR name, LPHANDLE printer, LPPRINTER_DEFAULTSA defaults);
WINSPOOLAPI BOOL WINAPI OpenPrinterW(LPWSTR name, LPHANDLE printer, LPPRINTER_DEFAULTSW defaults);
WINSPOOLAPI BOOL WINAPI OpenPrinter2A(LPCSTR name, LPHANDLE printer, PPRINTER_DEFAULTSA defaults,
                                      PPRINTER_OPTIONSA options);
WINSPOOLAPI BOOL WINAPI OpenPrinter2W(LPCWSTR name, LPHANDLE printer, PPRINTER_DEFAULTSW defaults,
                                      PPRINTER_OPTIONSW options);
WINSPOOLAPI BOOL WINAPI ClosePrinter(HANDLE printer);
WINSPOOLAPI BOOL WINAPI GetPrinterA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed);
WINSPOOLAPI BOOL WINAPI GetPrinterW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed);
WINSPOOLAPI BOOL WINAPI SetPrinterA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD command);
WINSPOOLAPI BOOL WINAPI SetPrinterW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD command);
WINSPOOLAPI BOOL WINAPI GetDefaultPrinterA(LPSTR buffer, LPDWORD size);
WINSPOOLAPI BOOL WINAPI GetDefaultPrinterW(LPWSTR buffer, LPDWORD size);
WINSPOOLAPI BOOL WINAPI SetDefaultPrinterA(LPCSTR printer);
WINSPOOLAPI BOOL WINAPI SetDefaultPrinterW(LPCWSTR printer);
WINSPOOLAPI LONG WINAPI DocumentPropertiesA(HWND window, HANDLE printer, LPSTR device, PDEVMODEA output,
                                            PDEVMODEA input, DWORD mode);
WINSPOOLAPI LONG WINAPI DocumentPropertiesW(HWND window, HANDLE printer, LPWSTR device, PDEVMODEW output,
                                            PDEVMODEW input, DWORD mode);
WINSPOOLAPI int WINAPI DeviceCapabilitiesA(LPCSTR device, LPCSTR port, WORD capability, LPSTR output,
                                           const DEVMODEA *devmode);
WINSPOOLAPI int WINAPI DeviceCapabilitiesW(LPCWSTR device, LPCWSTR port, WORD capability, LPWSTR output,
                                           const DEVMODEW *devmode);
WINSPOOLAPI BOOL WINAPI IsValidDevmodeA(PDEVMODEA devmode, SIZE_T size);
WINSPOOLAPI BOOL WINAPI IsValidDevmodeW(PDEVMODEW devmode, SIZE_T size);
WINSPOOLAPI DWORD WINAPI StartDocPrinterA(HANDLE printer, DWORD level, LPBYTE info);
WINSPOOLAPI DWORD WINAPI StartDocPrinterW(HANDLE printer, DWORD level, LPBYTE info);
WINSPOOLAPI BOOL WINAPI StartPagePrinter(HANDLE printer);
WINSPOOLAPI BOOL WINAPI EndPagePrinter(HANDLE printer);
WINSPOOLAPI BOOL WINAPI EndDocPrinter(HANDLE printer);
WINSPOOLAPI BOOL WINAPI AbortPrinter(HANDLE printer);
WINSPOOLAPI BOOL WINAPI WritePrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD written);
WINSPOOLAPI BOOL WINAPI ReadPrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD read);
WINSPOOLAPI BOOL WINAPI EnumFormsA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                   LPDWORD returned);
WINSPOOLAPI BOOL WINAPI EnumFormsW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                   LPDWORD returned);
WINSPOOLAPI BOOL WINAPI EnumJobsA(HANDLE printer, DWORD first, DWORD count, DWORD level, LPBYTE buffer, DWORD size,
                                  LPDWORD needed, LPDWORD returned);
WINSPOOLAPI BOOL WINAPI EnumJobsW(HANDLE printer, DWORD first, DWORD count, DWORD level, LPBYTE buffer, DWORD size,
                                  LPDWORD needed, LPDWORD returned);
WINSPOOLAPI BOOL WINAPI EnumPrinterDriversA(LPSTR name, LPSTR environment, DWORD level, LPBYTE buffer, DWORD size,
                                            LPDWORD needed, LPDWORD returned);
WINSPOOLAPI BOOL WINAPI EnumPrinterDriversW(LPWSTR name, LPWSTR environment, DWORD level, LPBYTE buffer, DWORD size,
                                            LPDWORD needed, LPDWORD returned);
WINSPOOLAPI BOOL WINAPI GetPrinterDriverA(HANDLE printer, LPSTR environment, DWORD level, LPBYTE buffer, DWORD size,
                                          LPDWORD needed);
WINSPOOLAPI BOOL WINAPI GetPrinterDriverW(HANDLE printer, LPWSTR environment, DWORD level, LPBYTE buffer,
                                          DWORD size, LPDWORD needed);
WINSPOOLAPI BOOL WINAPI GetPrinterDriverDirectoryA(LPSTR name, LPSTR environment, DWORD level, LPBYTE buffer,
                                                   DWORD size, LPDWORD needed);
WINSPOOLAPI BOOL WINAPI GetPrinterDriverDirectoryW(LPWSTR name, LPWSTR environment, DWORD level, LPBYTE buffer,
                                                   DWORD size, LPDWORD needed);
WINSPOOLAPI HANDLE WINAPI AddPrinterA(LPSTR name, DWORD level, LPBYTE printer);
WINSPOOLAPI HANDLE WINAPI AddPrinterW(LPWSTR name, DWORD level, LPBYTE printer);
WINSPOOLAPI BOOL WINAPI DeletePrinter(HANDLE printer);
WINSPOOLAPI DWORD WINAPI GetPrinterDataA(HANDLE printer, LPSTR name, LPDWORD type, LPBYTE data, DWORD size,
                                         LPDWORD needed);
WINSPOOLAPI DWORD WINAPI GetPrinterDataW(HANDLE printer, LPWSTR name, LPDWORD type, LPBYTE data, DWORD size,
                                         LPDWORD needed);

#endif
