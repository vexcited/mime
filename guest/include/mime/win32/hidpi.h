/* hid.dll: HidD_ functions talk to a hid collection, HidP_ functions parse reports with its preparsed data. */
#ifndef MIME_WIN32_HIDPI_H
#define MIME_WIN32_HIDPI_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_HID
#define HIDAPI DECLSPEC_EXPORT
#else
#define HIDAPI DECLSPEC_IMPORT
#endif

typedef USHORT USAGE, *PUSAGE;
typedef struct _HIDP_PREPARSED_DATA *PHIDP_PREPARSED_DATA;

typedef enum _HIDP_REPORT_TYPE { HidP_Input, HidP_Output, HidP_Feature } HIDP_REPORT_TYPE;
typedef enum _HIDP_KEYBOARD_DIRECTION { HidP_Keyboard_Break, HidP_Keyboard_Make } HIDP_KEYBOARD_DIRECTION;

#define HIDP_LINK_COLLECTION_ROOT ((USHORT)-1)
#define HIDP_LINK_COLLECTION_UNSPECIFIED ((USHORT)0)

#define HIDP_STATUS_SUCCESS ((NTSTATUS)0x00110000)
#define HIDP_STATUS_NULL ((NTSTATUS)0x80110001)
#define HIDP_STATUS_INVALID_PREPARSED_DATA ((NTSTATUS)0xC0110001)
#define HIDP_STATUS_INVALID_REPORT_TYPE ((NTSTATUS)0xC0110002)
#define HIDP_STATUS_INVALID_REPORT_LENGTH ((NTSTATUS)0xC0110003)
#define HIDP_STATUS_USAGE_NOT_FOUND ((NTSTATUS)0xC0110004)
#define HIDP_STATUS_VALUE_OUT_OF_RANGE ((NTSTATUS)0xC0110005)
#define HIDP_STATUS_BAD_LOG_PHY_VALUES ((NTSTATUS)0xC0110006)
#define HIDP_STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0110007)
#define HIDP_STATUS_INTERNAL_ERROR ((NTSTATUS)0xC0110008)
#define HIDP_STATUS_I8042_TRANS_UNKNOWN ((NTSTATUS)0xC0110009)
#define HIDP_STATUS_INCOMPATIBLE_REPORT_ID ((NTSTATUS)0xC011000A)
#define HIDP_STATUS_NOT_VALUE_ARRAY ((NTSTATUS)0xC011000B)
#define HIDP_STATUS_IS_VALUE_ARRAY ((NTSTATUS)0xC011000C)
#define HIDP_STATUS_DATA_INDEX_NOT_FOUND ((NTSTATUS)0xC011000D)
#define HIDP_STATUS_DATA_INDEX_OUT_OF_RANGE ((NTSTATUS)0xC011000E)
#define HIDP_STATUS_BUTTON_NOT_PRESSED ((NTSTATUS)0xC011000F)
#define HIDP_STATUS_REPORT_DOES_NOT_EXIST ((NTSTATUS)0xC0110010)
#define HIDP_STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0110020)
#define HIDP_STATUS_NOT_BUTTON_ARRAY ((NTSTATUS)0xC0110021)

typedef struct _HIDD_ATTRIBUTES {
    ULONG Size;
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
} HIDD_ATTRIBUTES, *PHIDD_ATTRIBUTES;

typedef struct _HIDD_CONFIGURATION {
    PVOID cookie;
    ULONG size;
    ULONG RingBufferSize;
} HIDD_CONFIGURATION, *PHIDD_CONFIGURATION;

typedef struct _HIDP_CAPS {
    USAGE Usage;
    USAGE UsagePage;
    USHORT InputReportByteLength;
    USHORT OutputReportByteLength;
    USHORT FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT NumberInputButtonCaps;
    USHORT NumberInputValueCaps;
    USHORT NumberInputDataIndices;
    USHORT NumberOutputButtonCaps;
    USHORT NumberOutputValueCaps;
    USHORT NumberOutputDataIndices;
    USHORT NumberFeatureButtonCaps;
    USHORT NumberFeatureValueCaps;
    USHORT NumberFeatureDataIndices;
} HIDP_CAPS, *PHIDP_CAPS;

typedef struct _HIDP_CAPS_RANGE {
    USAGE UsageMin, UsageMax;
    USHORT StringMin, StringMax;
    USHORT DesignatorMin, DesignatorMax;
    USHORT DataIndexMin, DataIndexMax;
} HIDP_CAPS_RANGE;

typedef struct _HIDP_CAPS_NOT_RANGE {
    USAGE Usage, Reserved1;
    USHORT StringIndex, Reserved2;
    USHORT DesignatorIndex, Reserved3;
    USHORT DataIndex, Reserved4;
} HIDP_CAPS_NOT_RANGE;

typedef struct _HIDP_BUTTON_CAPS {
    USAGE UsagePage;
    UCHAR ReportID;
    BOOLEAN IsAlias;
    USHORT BitField;
    USHORT LinkCollection;
    USAGE LinkUsage;
    USAGE LinkUsagePage;
    BOOLEAN IsRange;
    BOOLEAN IsStringRange;
    BOOLEAN IsDesignatorRange;
    BOOLEAN IsAbsolute;
    USHORT ReportCount;
    USHORT Reserved2;
    ULONG Reserved[9];
    union {
        HIDP_CAPS_RANGE Range;
        HIDP_CAPS_NOT_RANGE NotRange;
    };
} HIDP_BUTTON_CAPS, *PHIDP_BUTTON_CAPS;

typedef struct _HIDP_VALUE_CAPS {
    USAGE UsagePage;
    UCHAR ReportID;
    BOOLEAN IsAlias;
    USHORT BitField;
    USHORT LinkCollection;
    USAGE LinkUsage;
    USAGE LinkUsagePage;
    BOOLEAN IsRange;
    BOOLEAN IsStringRange;
    BOOLEAN IsDesignatorRange;
    BOOLEAN IsAbsolute;
    BOOLEAN HasNull;
    UCHAR Reserved;
    USHORT BitSize;
    USHORT ReportCount;
    USHORT Reserved2[5];
    ULONG UnitsExp;
    ULONG Units;
    LONG LogicalMin, LogicalMax;
    LONG PhysicalMin, PhysicalMax;
    union {
        HIDP_CAPS_RANGE Range;
        HIDP_CAPS_NOT_RANGE NotRange;
    };
} HIDP_VALUE_CAPS, *PHIDP_VALUE_CAPS;

typedef struct _HIDP_LINK_COLLECTION_NODE {
    USAGE LinkUsage;
    USAGE LinkUsagePage;
    USHORT Parent;
    USHORT NumberOfChildren;
    USHORT NextSibling;
    USHORT FirstChild;
    ULONG CollectionType : 8;
    ULONG IsAlias : 1;
    ULONG Reserved : 23;
    PVOID UserContext;
} HIDP_LINK_COLLECTION_NODE, *PHIDP_LINK_COLLECTION_NODE;

typedef struct _HIDP_DATA {
    USHORT DataIndex;
    USHORT Reserved;
    union {
        ULONG RawValue;
        BOOLEAN On;
    };
} HIDP_DATA, *PHIDP_DATA;

typedef struct _USAGE_AND_PAGE {
    USAGE Usage;
    USAGE UsagePage;
} USAGE_AND_PAGE, *PUSAGE_AND_PAGE;

typedef struct _HIDP_BUTTON_ARRAY_DATA {
    USHORT ArrayIndex;
    BOOLEAN On;
} HIDP_BUTTON_ARRAY_DATA, *PHIDP_BUTTON_ARRAY_DATA;

typedef struct _HIDP_UNKNOWN_TOKEN {
    UCHAR Token;
    UCHAR Reserved[3];
    ULONG BitField;
} HIDP_UNKNOWN_TOKEN, *PHIDP_UNKNOWN_TOKEN;

typedef struct _HIDP_EXTENDED_ATTRIBUTES {
    UCHAR NumGlobalUnknowns;
    UCHAR Reserved[3];
    PHIDP_UNKNOWN_TOKEN GlobalUnknowns;
    ULONG Data[1];
} HIDP_EXTENDED_ATTRIBUTES, *PHIDP_EXTENDED_ATTRIBUTES;

typedef union _HIDP_KEYBOARD_MODIFIER_STATE {
    struct {
        ULONG LeftControl : 1;
        ULONG LeftShift : 1;
        ULONG LeftAlt : 1;
        ULONG LeftGUI : 1;
        ULONG RightControl : 1;
        ULONG RightShift : 1;
        ULONG RightAlt : 1;
        ULONG RigthGUI : 1;
        ULONG CapsLock : 1;
        ULONG ScollLock : 1;
        ULONG NumLock : 1;
        ULONG Reserved : 21;
    };
    ULONG ul;
} HIDP_KEYBOARD_MODIFIER_STATE, *PHIDP_KEYBOARD_MODIFIER_STATE;

typedef BOOLEAN(NTAPI *PHIDP_INSERT_SCANCODES)(PVOID context, PCHAR codes, ULONG length);

HIDAPI void NTAPI HidD_GetHidGuid(GUID *guid);
HIDAPI BOOLEAN NTAPI HidD_GetAttributes(HANDLE device, PHIDD_ATTRIBUTES attributes);
HIDAPI BOOLEAN NTAPI HidD_GetPreparsedData(HANDLE device, PHIDP_PREPARSED_DATA *data);
HIDAPI BOOLEAN NTAPI HidD_FreePreparsedData(PHIDP_PREPARSED_DATA data);
HIDAPI BOOLEAN NTAPI HidD_FlushQueue(HANDLE device);
HIDAPI BOOLEAN NTAPI HidD_GetNumInputBuffers(HANDLE device, PULONG count);
HIDAPI BOOLEAN NTAPI HidD_SetNumInputBuffers(HANDLE device, ULONG count);
HIDAPI BOOLEAN NTAPI HidD_GetInputReport(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_SetOutputReport(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetFeature(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_SetFeature(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetManufacturerString(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetProductString(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetSerialNumberString(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetIndexedString(HANDLE device, ULONG index, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetPhysicalDescriptor(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetMsGenreDescriptor(HANDLE device, PVOID buffer, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_GetConfiguration(HANDLE device, PHIDD_CONFIGURATION configuration, ULONG length);
HIDAPI BOOLEAN NTAPI HidD_SetConfiguration(HANDLE device, PHIDD_CONFIGURATION configuration, ULONG length);
HIDAPI ULONG NTAPI HidD_Hello(PCHAR buffer, ULONG length);

HIDAPI NTSTATUS NTAPI HidP_GetCaps(PHIDP_PREPARSED_DATA data, PHIDP_CAPS caps);
HIDAPI NTSTATUS NTAPI HidP_GetButtonCaps(HIDP_REPORT_TYPE type, PHIDP_BUTTON_CAPS caps, PUSHORT length,
                                         PHIDP_PREPARSED_DATA data);
HIDAPI NTSTATUS NTAPI HidP_GetSpecificButtonCaps(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                                 PHIDP_BUTTON_CAPS caps, PUSHORT length, PHIDP_PREPARSED_DATA data);
HIDAPI NTSTATUS NTAPI HidP_GetValueCaps(HIDP_REPORT_TYPE type, PHIDP_VALUE_CAPS caps, PUSHORT length,
                                        PHIDP_PREPARSED_DATA data);
HIDAPI NTSTATUS NTAPI HidP_GetSpecificValueCaps(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                                PHIDP_VALUE_CAPS caps, PUSHORT length, PHIDP_PREPARSED_DATA data);
HIDAPI NTSTATUS NTAPI HidP_GetLinkCollectionNodes(PHIDP_LINK_COLLECTION_NODE nodes, PULONG length,
                                                  PHIDP_PREPARSED_DATA data);
HIDAPI ULONG NTAPI HidP_MaxUsageListLength(HIDP_REPORT_TYPE type, USAGE page, PHIDP_PREPARSED_DATA data);
HIDAPI ULONG NTAPI HidP_MaxDataListLength(HIDP_REPORT_TYPE type, PHIDP_PREPARSED_DATA data);
HIDAPI NTSTATUS NTAPI HidP_GetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list, PULONG length,
                                     PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_GetUsagesEx(HIDP_REPORT_TYPE type, USHORT link, PUSAGE_AND_PAGE list, PULONG length,
                                       PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_SetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list, PULONG length,
                                     PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_UnsetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list, PULONG length,
                                       PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_GetButtonArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                          PHIDP_BUTTON_ARRAY_DATA list, PUSHORT length, PHIDP_PREPARSED_DATA data,
                                          PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_SetButtonArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                          PHIDP_BUTTON_ARRAY_DATA list, USHORT length, PHIDP_PREPARSED_DATA data,
                                          PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_GetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage, PULONG value,
                                         PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_GetScaledUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                               PLONG value, PHIDP_PREPARSED_DATA data, PCHAR report,
                                               ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_GetUsageValueArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                              PCHAR buffer, USHORT buffer_length, PHIDP_PREPARSED_DATA data,
                                              PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_SetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage, ULONG value,
                                         PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_SetScaledUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                               LONG value, PHIDP_PREPARSED_DATA data, PCHAR report,
                                               ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_SetUsageValueArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                              PCHAR buffer, USHORT buffer_length, PHIDP_PREPARSED_DATA data,
                                              PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_GetData(HIDP_REPORT_TYPE type, PHIDP_DATA list, PULONG length, PHIDP_PREPARSED_DATA data,
                                   PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_SetData(HIDP_REPORT_TYPE type, PHIDP_DATA list, PULONG length, PHIDP_PREPARSED_DATA data,
                                   PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_InitializeReportForID(HIDP_REPORT_TYPE type, UCHAR id, PHIDP_PREPARSED_DATA data,
                                                 PCHAR report, ULONG report_length);
HIDAPI NTSTATUS NTAPI HidP_UsageListDifference(PUSAGE previous, PUSAGE current, PUSAGE broken, PUSAGE made,
                                               ULONG length);
HIDAPI NTSTATUS NTAPI HidP_TranslateUsagesToI8042ScanCodes(PUSAGE list, ULONG length, HIDP_KEYBOARD_DIRECTION action,
                                                           PHIDP_KEYBOARD_MODIFIER_STATE state,
                                                           PHIDP_INSERT_SCANCODES insert, PVOID context);
HIDAPI NTSTATUS NTAPI HidP_GetExtendedAttributes(HIDP_REPORT_TYPE type, USHORT index, PHIDP_PREPARSED_DATA data,
                                                 PHIDP_EXTENDED_ATTRIBUTES attributes, PULONG length);
HIDAPI NTSTATUS NTAPI HidP_GetVersionInternal(PULONG version);

#endif
