/* the bluetooth api, bluetoothapis.h and bthsdpdef.h. */
#ifndef MIME_WIN32_BLUETOOTHAPIS_H
#define MIME_WIN32_BLUETOOTHAPIS_H

#include <mime/win32/winbase.h>

#ifndef BLUETOOTHAPI
#define BLUETOOTHAPI DECLSPEC_IMPORT
#endif

typedef HANDLE HBLUETOOTH_RADIO_FIND, HBLUETOOTH_DEVICE_FIND, HBLUETOOTH_CONTAINER_ELEMENT;
typedef ULONGLONG BTH_ADDR __attribute__((aligned(8)));

typedef struct _BLUETOOTH_ADDRESS {
    union {
        BTH_ADDR ullLong;
        BYTE rgBytes[6];
    };
} BLUETOOTH_ADDRESS;

typedef struct _BLUETOOTH_FIND_RADIO_PARAMS {
    DWORD dwSize;
} BLUETOOTH_FIND_RADIO_PARAMS;

#define BLUETOOTH_MAX_NAME_SIZE 248

typedef struct _BLUETOOTH_RADIO_INFO {
    DWORD dwSize;
    BLUETOOTH_ADDRESS address;
    WCHAR szName[BLUETOOTH_MAX_NAME_SIZE];
    ULONG ulClassofDevice;
    USHORT lmpSubversion;
    USHORT manufacturer;
} BLUETOOTH_RADIO_INFO, *PBLUETOOTH_RADIO_INFO;

typedef struct _BLUETOOTH_DEVICE_INFO {
    DWORD dwSize;
    BLUETOOTH_ADDRESS Address;
    ULONG ulClassofDevice;
    BOOL fConnected;
    BOOL fRemembered;
    BOOL fAuthenticated;
    SYSTEMTIME stLastSeen;
    SYSTEMTIME stLastUsed;
    WCHAR szName[BLUETOOTH_MAX_NAME_SIZE];
} BLUETOOTH_DEVICE_INFO_STRUCT, BLUETOOTH_DEVICE_INFO, *PBLUETOOTH_DEVICE_INFO;

typedef struct _BLUETOOTH_DEVICE_SEARCH_PARAMS {
    DWORD dwSize;
    BOOL fReturnAuthenticated;
    BOOL fReturnRemembered;
    BOOL fReturnUnknown;
    BOOL fReturnConnected;
    BOOL fIssueInquiry;
    UCHAR cTimeoutMultiplier;
    HANDLE hRadio;
} BLUETOOTH_DEVICE_SEARCH_PARAMS;

typedef enum _SDP_TYPE {
    SDP_TYPE_NIL = 0x00,
    SDP_TYPE_UINT = 0x01,
    SDP_TYPE_INT = 0x02,
    SDP_TYPE_UUID = 0x03,
    SDP_TYPE_STRING = 0x04,
    SDP_TYPE_BOOLEAN = 0x05,
    SDP_TYPE_SEQUENCE = 0x06,
    SDP_TYPE_ALTERNATIVE = 0x07,
    SDP_TYPE_URL = 0x08,
    SDP_TYPE_CONTAINER = 0x20,
} SDP_TYPE;

typedef enum _SDP_SPECIFICTYPE {
    SDP_ST_NONE = 0x0000,
    SDP_ST_UINT8 = 0x0010,
    SDP_ST_UINT16 = 0x0110,
    SDP_ST_UINT32 = 0x0210,
    SDP_ST_UINT64 = 0x0310,
    SDP_ST_UINT128 = 0x0410,
    SDP_ST_INT8 = 0x0020,
    SDP_ST_INT16 = 0x0120,
    SDP_ST_INT32 = 0x0220,
    SDP_ST_INT64 = 0x0320,
    SDP_ST_INT128 = 0x0420,
    SDP_ST_UUID16 = 0x0130,
    SDP_ST_UUID32 = 0x0220,
    SDP_ST_UUID128 = 0x0430,
} SDP_SPECIFICTYPE;

typedef struct _SDP_LARGE_INTEGER_16 {
    ULONGLONG LowPart;
    LONGLONG HighPart;
} SDP_LARGE_INTEGER_16;

typedef struct _SDP_ELEMENT_DATA {
    SDP_TYPE type;
    SDP_SPECIFICTYPE specificType;
    union {
        SDP_LARGE_INTEGER_16 int128;
        LONGLONG int64;
        LONG int32;
        SHORT int16;
        CHAR int8;
        SDP_LARGE_INTEGER_16 uint128;
        ULONGLONG uint64;
        ULONG uint32;
        USHORT uint16;
        UCHAR uint8;
        UCHAR booleanVal;
        GUID uuid128;
        ULONG uuid32;
        USHORT uuid16;
        struct {
            LPBYTE value;
            ULONG length;
        } string, url, sequence, alternative;
    } data;
} SDP_ELEMENT_DATA, *PSDP_ELEMENT_DATA;

_Static_assert(sizeof(BLUETOOTH_DEVICE_INFO) == 560, "the size of the device record");
_Static_assert(sizeof(BLUETOOTH_RADIO_INFO) == 520, "the size of the radio record");

BLUETOOTHAPI HBLUETOOTH_RADIO_FIND WINAPI BluetoothFindFirstRadio(const BLUETOOTH_FIND_RADIO_PARAMS *params,
                                                                  HANDLE *radio);
BLUETOOTHAPI BOOL WINAPI BluetoothFindRadioClose(HBLUETOOTH_RADIO_FIND find);
BLUETOOTHAPI DWORD WINAPI BluetoothGetRadioInfo(HANDLE radio, PBLUETOOTH_RADIO_INFO info);
BLUETOOTHAPI HBLUETOOTH_DEVICE_FIND WINAPI BluetoothFindFirstDevice(const BLUETOOTH_DEVICE_SEARCH_PARAMS *params,
                                                                    BLUETOOTH_DEVICE_INFO *info);
BLUETOOTHAPI BOOL WINAPI BluetoothFindDeviceClose(HBLUETOOTH_DEVICE_FIND find);
BLUETOOTHAPI BOOL WINAPI BluetoothIsConnectable(HANDLE radio);
BLUETOOTHAPI BOOL WINAPI BluetoothEnableDiscovery(HANDLE radio, BOOL enabled);
BLUETOOTHAPI DWORD WINAPI BluetoothSdpGetElementData(LPBYTE stream, ULONG length, PSDP_ELEMENT_DATA data);
BLUETOOTHAPI DWORD WINAPI BluetoothSdpGetContainerElementData(LPBYTE stream, ULONG length,
                                                              HBLUETOOTH_CONTAINER_ELEMENT *element,
                                                              PSDP_ELEMENT_DATA data);
BLUETOOTHAPI DWORD WINAPI BluetoothSdpGetAttributeValue(LPBYTE record, ULONG length, USHORT id,
                                                        PSDP_ELEMENT_DATA data);

#endif
