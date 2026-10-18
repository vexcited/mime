/*
 * print spooler api of winspool.drv: printer handles, enumeration, jobs,
 * forms, drivers and device modes.
 */
#ifndef MIME_WIN32_WINSPOOL_H
#define MIME_WIN32_WINSPOOL_H

#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>

#ifdef MIME_BUILD_WINSPOOL
#define WINSPOOLAPI DECLSPEC_EXPORT
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

/* the values of the device mode members. */
#define DMORIENT_PORTRAIT 1
#define DMORIENT_LANDSCAPE 2
#define DMPAPER_LETTER 1
#define DMPAPER_LETTERSMALL 2
#define DMPAPER_TABLOID 3
#define DMPAPER_LEDGER 4
#define DMPAPER_LEGAL 5
#define DMPAPER_STATEMENT 6
#define DMPAPER_EXECUTIVE 7
#define DMPAPER_A3 8
#define DMPAPER_A4 9
#define DMPAPER_A4SMALL 10
#define DMPAPER_A5 11
#define DMPAPER_B4 12
#define DMPAPER_B5 13
#define DMPAPER_FOLIO 14
#define DMPAPER_QUARTO 15
#define DMPAPER_10X14 16
#define DMPAPER_11X17 17
#define DMPAPER_NOTE 18
#define DMPAPER_ENV_9 19
#define DMPAPER_ENV_10 20
#define DMPAPER_ENV_DL 27
#define DMPAPER_ENV_C5 28
#define DMPAPER_A6 70
#define DMPAPER_USER 256
#define DMBIN_UPPER 1
#define DMBIN_ONLYONE 1
#define DMBIN_LOWER 2
#define DMBIN_MIDDLE 3
#define DMBIN_MANUAL 4
#define DMBIN_AUTO 7
#define DMBIN_USER 256
#define DMRES_DRAFT (-1)
#define DMRES_LOW (-2)
#define DMRES_MEDIUM (-3)
#define DMRES_HIGH (-4)
#define DMCOLOR_MONOCHROME 1
#define DMCOLOR_COLOR 2
#define DMDUP_SIMPLEX 1
#define DMDUP_VERTICAL 2
#define DMDUP_HORIZONTAL 3
#define DMTT_BITMAP 1
#define DMTT_DOWNLOAD 2
#define DMTT_SUBDEV 3
#define DMCOLLATE_FALSE 0
#define DMCOLLATE_TRUE 1
#define DMICMMETHOD_NONE 1
#define DMICMMETHOD_SYSTEM 2
#define DMICM_CONTRAST 2
#define DMMEDIA_STANDARD 1
#define DMDITHER_NONE 1
#define DMDITHER_FINE 3

#define DC_COLLATE 22
#define DC_MANUFACTURER 23
#define DC_MODEL 24
#define DC_PERSONALITY 25
#define DC_PRINTRATE 26
#define DC_PRINTRATEUNIT 27
#define DC_PRINTERMEM 28
#define DC_MEDIAREADY 29
#define DC_STAPLE 30
#define DC_PRINTRATEPPM 31
#define DC_COLORDEVICE 32
#define DC_NUP 33
#define DC_MEDIATYPENAMES 34
#define DC_MEDIATYPES 35
#define DC_DUPLEX_UNUSED 0
#define DCTT_BITMAP 1
#define DCTT_DOWNLOAD 2
#define DCTT_SUBDEV 4
#define DCTT_DOWNLOAD_OUTLINE 8

/* printer, job and port values. */
#define PRINTER_CONTROL_PAUSE 1
#define PRINTER_CONTROL_RESUME 2
#define PRINTER_CONTROL_PURGE 3
#define PRINTER_CONTROL_SET_STATUS 4
#define PRINTER_STATUS_PAUSED 0x00000001
#define PRINTER_STATUS_ERROR 0x00000002
#define PRINTER_STATUS_PENDING_DELETION 0x00000004
#define PRINTER_STATUS_PAPER_JAM 0x00000008
#define PRINTER_STATUS_PAPER_OUT 0x00000010
#define PRINTER_STATUS_OFFLINE 0x00000080
#define PRINTER_STATUS_BUSY 0x00000200
#define PRINTER_STATUS_PRINTING 0x00000400
#define PRINTER_STATUS_WAITING 0x00002000
#define PRINTER_STATUS_PROCESSING 0x00004000
#define PRINTER_ATTRIBUTE_QUEUED 0x00000001
#define PRINTER_ATTRIBUTE_DIRECT 0x00000002
#define PRINTER_ATTRIBUTE_DEFAULT 0x00000004
#define PRINTER_ATTRIBUTE_SHARED 0x00000008
#define PRINTER_ATTRIBUTE_NETWORK 0x00000010
#define PRINTER_ATTRIBUTE_HIDDEN 0x00000020
#define PRINTER_ATTRIBUTE_LOCAL 0x00000040
#define PRINTER_ATTRIBUTE_ENABLE_DEVQ 0x00000080
#define PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS 0x00000100
#define PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST 0x00000200
#define PRINTER_ATTRIBUTE_WORK_OFFLINE 0x00000400
#define PRINTER_ATTRIBUTE_ENABLE_BIDI 0x00000800
#define PRINTER_ATTRIBUTE_RAW_ONLY 0x00001000
#define PRINTER_ATTRIBUTE_PUBLISHED 0x00002000
#define JOB_CONTROL_PAUSE 1
#define JOB_CONTROL_RESUME 2
#define JOB_CONTROL_CANCEL 3
#define JOB_CONTROL_RESTART 4
#define JOB_CONTROL_DELETE 5
#define JOB_STATUS_PAUSED 0x00000001
#define JOB_STATUS_ERROR 0x00000002
#define JOB_STATUS_DELETING 0x00000004
#define JOB_STATUS_SPOOLING 0x00000008
#define JOB_STATUS_PRINTING 0x00000010
#define JOB_STATUS_OFFLINE 0x00000020
#define JOB_STATUS_PAPEROUT 0x00000040
#define JOB_STATUS_PRINTED 0x00000080
#define JOB_STATUS_DELETED 0x00000100
#define JOB_STATUS_COMPLETE 0x00001000
#define JOB_POSITION_UNSPECIFIED 0
#define NO_PRIORITY 0
#define DEF_PRIORITY 1
#define PRINTER_ENUM_ICON1 0x00010000
#define PRINTER_ENUM_ICON2 0x00020000
#define PRINTER_ENUM_ICON3 0x00040000
#define PRINTER_ENUM_ICON4 0x00080000
#define PRINTER_ENUM_ICON5 0x00100000
#define PRINTER_ENUM_ICON6 0x00200000
#define PRINTER_ENUM_ICON7 0x00400000
#define PRINTER_ENUM_ICON8 0x00800000
#define FORM_USER 0
#define FORM_BUILTIN 1
#define FORM_PRINTER 2
#define PORT_TYPE_WRITE 1
#define PORT_TYPE_READ 2
#define PORT_TYPE_REDIRECTED 4
#define PORT_TYPE_NET_ATTACHED 8
#ifndef ERROR_INVALID_DATATYPE
#define ERROR_INVALID_DATATYPE 1804L
#endif
#ifndef ERROR_SPL_NO_STARTDOC
#define ERROR_SPL_NO_STARTDOC 3004L
#endif
#ifndef ERROR_PRINTER_ALREADY_EXISTS
#define ERROR_PRINTER_ALREADY_EXISTS 1802L
#endif
#ifndef ERROR_PRINT_CANCELLED
#define ERROR_PRINT_CANCELLED 3008L
#endif
#ifndef ERROR_INVALID_PRINTER_COMMAND
#define ERROR_INVALID_PRINTER_COMMAND 1803L
#endif
#ifndef ERROR_PRINTER_DRIVER_ALREADY_INSTALLED
#define ERROR_PRINTER_DRIVER_ALREADY_INSTALLED 1795L
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

typedef struct _PRINTER_INFO_0W {
    LPWSTR pPrinterName;
    LPWSTR pServerName;
    DWORD cJobs;
    DWORD cTotalJobs;
    DWORD cTotalBytes;
    SYSTEMTIME stUpTime;
    DWORD MaxSpooling;
    DWORD cTotalPagesPrinted;
    DWORD dwStatus;
    DWORD dwAttributes;
    DWORD dwDriverVersion;
    DWORD dwDriverMajorVersion;
    DWORD dwDriverMinorVersion;
    DWORD dwDriverBuild;
    DWORD dwDriverMajorBuild;
} PRINTER_INFO_0W, *PPRINTER_INFO_0W;

typedef struct _PRINTER_INFO_1A {
    DWORD Flags;
    LPSTR pDescription;
    LPSTR pName;
    LPSTR pComment;
} PRINTER_INFO_1A, *PPRINTER_INFO_1A, *LPPRINTER_INFO_1A;

typedef struct _PRINTER_INFO_2A {
    LPSTR pServerName;
    LPSTR pPrinterName;
    LPSTR pShareName;
    LPSTR pPortName;
    LPSTR pDriverName;
    LPSTR pComment;
    LPSTR pLocation;
    LPDEVMODEA pDevMode;
    LPSTR pSepFile;
    LPSTR pPrintProcessor;
    LPSTR pDatatype;
    LPSTR pParameters;
    PSECURITY_DESCRIPTOR pSecurityDescriptor;
    DWORD Attributes;
    DWORD Priority;
    DWORD DefaultPriority;
    DWORD StartTime;
    DWORD UntilTime;
    DWORD Status;
    DWORD cJobs;
    DWORD AveragePPM;
} PRINTER_INFO_2A, *PPRINTER_INFO_2A, *LPPRINTER_INFO_2A;

typedef struct _PRINTER_INFO_4A {
    LPSTR pPrinterName;
    LPSTR pServerName;
    DWORD Attributes;
} PRINTER_INFO_4A, *PPRINTER_INFO_4A, *LPPRINTER_INFO_4A;

typedef struct _PRINTER_INFO_5W {
    LPWSTR pPrinterName;
    LPWSTR pPortName;
    DWORD Attributes;
    DWORD DeviceNotSelectedTimeout;
    DWORD TransmissionRetryTimeout;
} PRINTER_INFO_5W, *PPRINTER_INFO_5W, *LPPRINTER_INFO_5W;

typedef struct _PRINTER_INFO_5A {
    LPSTR pPrinterName;
    LPSTR pPortName;
    DWORD Attributes;
    DWORD DeviceNotSelectedTimeout;
    DWORD TransmissionRetryTimeout;
} PRINTER_INFO_5A, *PPRINTER_INFO_5A, *LPPRINTER_INFO_5A;

typedef struct _PRINTER_INFO_7W {
    LPWSTR pszObjectGUID;
    DWORD dwAction;
} PRINTER_INFO_7W, *PPRINTER_INFO_7W;

typedef struct _PRINTER_INFO_8W {
    LPDEVMODEW pDevMode;
} PRINTER_INFO_8W, *PPRINTER_INFO_8W;

typedef struct _PRINTER_INFO_9W {
    LPDEVMODEW pDevMode;
} PRINTER_INFO_9W, *PPRINTER_INFO_9W;

typedef struct _JOB_INFO_1W {
    DWORD JobId;
    LPWSTR pPrinterName;
    LPWSTR pMachineName;
    LPWSTR pUserName;
    LPWSTR pDocument;
    LPWSTR pDatatype;
    LPWSTR pStatus;
    DWORD Status;
    DWORD Priority;
    DWORD Position;
    DWORD TotalPages;
    DWORD PagesPrinted;
    SYSTEMTIME Submitted;
} JOB_INFO_1W, *PJOB_INFO_1W;

typedef struct _JOB_INFO_1A {
    DWORD JobId;
    LPSTR pPrinterName;
    LPSTR pMachineName;
    LPSTR pUserName;
    LPSTR pDocument;
    LPSTR pDatatype;
    LPSTR pStatus;
    DWORD Status;
    DWORD Priority;
    DWORD Position;
    DWORD TotalPages;
    DWORD PagesPrinted;
    SYSTEMTIME Submitted;
} JOB_INFO_1A, *PJOB_INFO_1A;

typedef struct _JOB_INFO_2W {
    DWORD JobId;
    LPWSTR pPrinterName;
    LPWSTR pMachineName;
    LPWSTR pUserName;
    LPWSTR pDocument;
    LPWSTR pNotifyName;
    LPWSTR pDatatype;
    LPWSTR pPrintProcessor;
    LPWSTR pParameters;
    LPWSTR pDriverName;
    LPDEVMODEW pDevMode;
    LPWSTR pStatus;
    PSECURITY_DESCRIPTOR pSecurityDescriptor;
    DWORD Status;
    DWORD Priority;
    DWORD Position;
    DWORD StartTime;
    DWORD UntilTime;
    DWORD TotalPages;
    DWORD Size;
    SYSTEMTIME Submitted;
    DWORD Time;
    DWORD PagesPrinted;
} JOB_INFO_2W, *PJOB_INFO_2W;

typedef struct _ADDJOB_INFO_1W {
    LPWSTR Path;
    DWORD JobId;
} ADDJOB_INFO_1W, *PADDJOB_INFO_1W;

typedef struct _DRIVER_INFO_1W {
    LPWSTR pName;
} DRIVER_INFO_1W, *PDRIVER_INFO_1W;

typedef struct _DRIVER_INFO_2W {
    DWORD cVersion;
    LPWSTR pName;
    LPWSTR pEnvironment;
    LPWSTR pDriverPath;
    LPWSTR pDataFile;
    LPWSTR pConfigFile;
} DRIVER_INFO_2W, *PDRIVER_INFO_2W;

typedef struct _DRIVER_INFO_3W {
    DWORD cVersion;
    LPWSTR pName;
    LPWSTR pEnvironment;
    LPWSTR pDriverPath;
    LPWSTR pDataFile;
    LPWSTR pConfigFile;
    LPWSTR pHelpFile;
    LPWSTR pDependentFiles;
    LPWSTR pMonitorName;
    LPWSTR pDefaultDataType;
} DRIVER_INFO_3W, *PDRIVER_INFO_3W;

typedef struct _PORT_INFO_1W {
    LPWSTR pName;
} PORT_INFO_1W, *PPORT_INFO_1W;

typedef struct _PORT_INFO_2W {
    LPWSTR pPortName;
    LPWSTR pMonitorName;
    LPWSTR pDescription;
    DWORD fPortType;
    DWORD Reserved;
} PORT_INFO_2W, *PPORT_INFO_2W;

typedef struct _MONITOR_INFO_1W {
    LPWSTR pName;
} MONITOR_INFO_1W, *PMONITOR_INFO_1W;

typedef struct _PRINTPROCESSOR_INFO_1W {
    LPWSTR pName;
} PRINTPROCESSOR_INFO_1W, *PPRINTPROCESSOR_INFO_1W;

typedef struct _DATATYPES_INFO_1W {
    LPWSTR pName;
} DATATYPES_INFO_1W, *PDATATYPES_INFO_1W;

typedef struct _FORM_INFO_1A {
    DWORD Flags;
    LPSTR pName;
    SIZEL Size;
    RECTL ImageableArea;
} FORM_INFO_1A, *PFORM_INFO_1A, *LPFORM_INFO_1A;

typedef struct _FORM_INFO_2W {
    DWORD Flags;
    LPCWSTR pName;
    SIZEL Size;
    RECTL ImageableArea;
    PCSTR pKeyword;
    DWORD StringType;
    PCWSTR pMuiDll;
    DWORD dwResourceId;
    PCWSTR pDisplayName;
    LANGID wLangId;
} FORM_INFO_2W, *PFORM_INFO_2W;

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

WINSPOOLAPI BOOL WINAPI GetJobW(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD size,
                                LPDWORD needed);
WINSPOOLAPI BOOL WINAPI SetJobW(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD command);
WINSPOOLAPI BOOL WINAPI GetFormW(HANDLE printer, LPWSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                 LPDWORD needed);
WINSPOOLAPI BOOL WINAPI EnumPortsW(LPWSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                   LPDWORD returned);
WINSPOOLAPI LPWSTR WINAPI StartDocDlgW(HANDLE printer, DOC_INFO_1W *info);
WINSPOOLAPI LONG WINAPI AdvancedDocumentPropertiesW(HWND window, HANDLE printer, LPWSTR device, PDEVMODEW output,
                                                    PDEVMODEW input);

#endif
