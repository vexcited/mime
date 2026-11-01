/*
 * the bluetooth api of a machine without a radio. windows forwards these to bluetoothapis.dll. radio searches find
 * nothing, device searches end at once, and the sdp record parsers are real.
 */
#define BLUETOOTHAPI
#include <mime/win32/bluetoothapis.h>

#define ERROR_REVISION_MISMATCH 1306L

DWORD service_error(void);

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/* ------------------------------------------------------------------ radios and devices */

HBLUETOOTH_RADIO_FIND WINAPI BluetoothFindFirstRadio(const BLUETOOTH_FIND_RADIO_PARAMS *params, HANDLE *radio)
{
    if (!params) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (params->dwSize != sizeof(*params)) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return nullptr;
    }
    SetLastError(ERROR_NO_MORE_ITEMS);
    return nullptr;
}

BOOL WINAPI BluetoothFindNextRadio(HBLUETOOTH_RADIO_FIND find, HANDLE *radio)
{
    SetLastError(find ? ERROR_NO_MORE_ITEMS : ERROR_INVALID_HANDLE);
    return FALSE;
}

/* no search ever starts, so every search handle is invalid. */
BOOL WINAPI BluetoothFindRadioClose(HBLUETOOTH_RADIO_FIND find)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

DWORD WINAPI BluetoothGetRadioInfo(HANDLE radio, PBLUETOOTH_RADIO_INFO info)
{
    if (!radio || !info)
        return ERROR_INVALID_PARAMETER;
    if (info->dwSize != sizeof(*info))
        return ERROR_REVISION_MISMATCH;
    return ERROR_INVALID_HANDLE;
}

HBLUETOOTH_DEVICE_FIND WINAPI BluetoothFindFirstDevice(const BLUETOOTH_DEVICE_SEARCH_PARAMS *params,
                                                       BLUETOOTH_DEVICE_INFO *info)
{
    if (!params || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (params->dwSize != sizeof(*params) || info->dwSize != sizeof(*info)) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return nullptr;
    }
    SetLastError(ERROR_NO_MORE_ITEMS);
    return nullptr;
}

BOOL WINAPI BluetoothFindNextDevice(HBLUETOOTH_DEVICE_FIND find, BLUETOOTH_DEVICE_INFO *info)
{
    SetLastError(find ? ERROR_NO_MORE_ITEMS : ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL WINAPI BluetoothFindDeviceClose(HBLUETOOTH_DEVICE_FIND find)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

DWORD WINAPI BluetoothGetDeviceInfo(HANDLE radio, BLUETOOTH_DEVICE_INFO *info)
{
    if (!info)
        return ERROR_INVALID_PARAMETER;
    if (info->dwSize != sizeof(*info))
        return ERROR_REVISION_MISMATCH;
    return ERROR_NOT_FOUND;
}

/* both go through the bluetooth support service, which is not running. */
BOOL WINAPI BluetoothEnableDiscovery(HANDLE radio, BOOL enabled)
{
    SetLastError(service_error());
    return FALSE;
}

BOOL WINAPI BluetoothEnableIncomingConnections(HANDLE radio, BOOL enabled)
{
    SetLastError(service_error());
    return FALSE;
}

BOOL WINAPI BluetoothIsConnectable(HANDLE radio)
{
    return FALSE;
}

BOOL WINAPI BluetoothIsDiscoverable(HANDLE radio)
{
    return FALSE;
}

/* the stack claims bluetooth 2.1. */
BOOL WINAPI BluetoothIsVersionAvailable(UCHAR major, UCHAR minor)
{
    return major < 2 || (major == 2 && minor <= 1);
}

/* the control panel applet refuses to start without a radio. */
LONG WINAPI CPlApplet(HWND hwnd, UINT message, LPARAM param1, LPARAM param2)
{
    return 0;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_OK;
}

/* ------------------------------------------------------------------ sdp records */

/* decodes a data element header: its size and where its value starts. */
static BOOL element_header(const BYTE *stream, ULONG length, BYTE *type, BYTE *index, ULONG *size, ULONG *header)
{
    static const ULONG sizes[] = {1, 2, 4, 8, 16};
    if (!length)
        return FALSE;
    *type = stream[0] >> 3;
    *index = stream[0] & 7;
    *header = 1;
    if (*index < 5) {
        *size = *type == SDP_TYPE_NIL ? 0 : sizes[*index];
    } else {
        ULONG bytes = 1u << (*index - 5);
        if (length < 1 + bytes)
            return FALSE;
        *size = 0;
        for (ULONG i = 0; i < bytes; i++)
            *size = *size << 8 | stream[1 + i];
        *header += bytes;
    }
    return *size <= length - *header;
}

static ULONGLONG big_endian(const BYTE *p, ULONG size)
{
    ULONGLONG value = 0;
    for (ULONG i = 0; i < size; i++)
        value = value << 8 | p[i];
    return value;
}

DWORD WINAPI BluetoothSdpGetElementData(LPBYTE stream, ULONG length, PSDP_ELEMENT_DATA data)
{
    if (!stream || !length || !data)
        return ERROR_INVALID_PARAMETER;
    BYTE type, index;
    ULONG size, header;
    if (!element_header(stream, length, &type, &index, &size, &header))
        return ERROR_INVALID_PARAMETER;
    const BYTE *value = stream + header;
    memset(data, 0, sizeof(*data));
    data->type = (SDP_TYPE)type;
    switch (type) {
    case SDP_TYPE_NIL:
        if (index)
            return ERROR_INVALID_PARAMETER;
        return ERROR_SUCCESS;
    case SDP_TYPE_UINT:
    case SDP_TYPE_INT:
        if (index > 4)
            return ERROR_INVALID_PARAMETER;
        data->specificType = (SDP_SPECIFICTYPE)((index << 8) | (type == SDP_TYPE_UINT ? 0x10 : 0x20));
        if (index == 4) {
            data->data.uint128.HighPart = (LONGLONG)big_endian(value, 8);
            data->data.uint128.LowPart = big_endian(value + 8, 8);
        } else {
            ULONGLONG bits = big_endian(value, size);
            switch (index) {
            case 0: data->data.uint8 = (UCHAR)bits; break;
            case 1: data->data.uint16 = (USHORT)bits; break;
            case 2: data->data.uint32 = (ULONG)bits; break;
            default: data->data.uint64 = bits; break;
            }
        }
        return ERROR_SUCCESS;
    case SDP_TYPE_UUID:
        if (index == 1) {
            data->specificType = SDP_ST_UUID16;
            data->data.uuid16 = (USHORT)big_endian(value, 2);
        } else if (index == 2) {
            data->specificType = SDP_ST_UUID32;
            data->data.uuid32 = (ULONG)big_endian(value, 4);
        } else if (index == 4) {
            data->specificType = SDP_ST_UUID128;
            data->data.uuid128.Data1 = (ULONG)big_endian(value, 4);
            data->data.uuid128.Data2 = (USHORT)big_endian(value + 4, 2);
            data->data.uuid128.Data3 = (USHORT)big_endian(value + 6, 2);
            memcpy(data->data.uuid128.Data4, value + 8, 8);
        } else {
            return ERROR_INVALID_PARAMETER;
        }
        return ERROR_SUCCESS;
    case SDP_TYPE_BOOLEAN:
        if (index)
            return ERROR_INVALID_PARAMETER;
        data->data.booleanVal = value[0];
        return ERROR_SUCCESS;
    case SDP_TYPE_STRING:
    case SDP_TYPE_URL:
    case SDP_TYPE_SEQUENCE:
    case SDP_TYPE_ALTERNATIVE:
        if (index < 5)
            return ERROR_INVALID_PARAMETER;
        data->data.string.value = (LPBYTE)value;
        data->data.string.length = size;
        return ERROR_SUCCESS;
    }
    return ERROR_INVALID_PARAMETER;
}

/* the element after current in a sequence or alternative, or the first for a null current. */
static BYTE *next_element(BYTE *stream, ULONG length, BYTE *current, ULONG *left)
{
    BYTE type, index;
    ULONG size, header;
    if (!element_header(stream, length, &type, &index, &size, &header))
        return nullptr;
    BYTE *start = stream + header, *end = start + size;
    BYTE *at = current ? current : start;
    if (current) {
        if (current < start || current >= end || !element_header(current, (ULONG)(end - current), &type, &index,
                                                                  &size, &header))
            return nullptr;
        at = current + header + size;
    }
    if (at >= end)
        return nullptr;
    *left = (ULONG)(end - at);
    return at;
}

DWORD WINAPI BluetoothSdpGetContainerElementData(LPBYTE stream, ULONG length, HBLUETOOTH_CONTAINER_ELEMENT *element,
                                                 PSDP_ELEMENT_DATA data)
{
    if (!stream || !length || !data || !element)
        return ERROR_INVALID_PARAMETER;
    SDP_ELEMENT_DATA container;
    DWORD error = BluetoothSdpGetElementData(stream, length, &container);
    if (error)
        return error;
    if (container.type != SDP_TYPE_SEQUENCE && container.type != SDP_TYPE_ALTERNATIVE)
        return ERROR_INVALID_PARAMETER;
    ULONG left;
    BYTE *next = next_element(stream, length, *element, &left);
    *element = next;
    if (!next)
        return ERROR_NO_MORE_ITEMS;
    return BluetoothSdpGetElementData(next, left, data);
}

/* a record is a sequence of attribute id and value pairs. */
DWORD WINAPI BluetoothSdpGetAttributeValue(LPBYTE record, ULONG length, USHORT id, PSDP_ELEMENT_DATA data)
{
    if (!record || !length || !data)
        return ERROR_INVALID_PARAMETER;
    SDP_ELEMENT_DATA element;
    DWORD error = BluetoothSdpGetElementData(record, length, &element);
    if (error)
        return error;
    if (element.type != SDP_TYPE_SEQUENCE)
        return ERROR_INVALID_PARAMETER;
    BYTE *at = nullptr;
    for (;;) {
        ULONG left;
        at = next_element(record, length, at, &left);
        if (!at)
            return ERROR_FILE_NOT_FOUND;
        if (BluetoothSdpGetElementData(at, left, &element) || element.type != SDP_TYPE_UINT ||
            element.specificType != SDP_ST_UINT16)
            return ERROR_INVALID_PARAMETER;
        USHORT attribute = element.data.uint16;
        at = next_element(record, length, at, &left);
        if (!at || BluetoothSdpGetElementData(at, left, &element))
            return ERROR_INVALID_PARAMETER;
        if (attribute == id) {
            *data = element;
            return ERROR_SUCCESS;
        }
    }
}

typedef BOOL(CALLBACK *ATTRIBUTE_CALLBACK)(ULONG id, LPBYTE value, ULONG size, LPVOID param);

BOOL WINAPI BluetoothSdpEnumAttributes(LPBYTE stream, ULONG length, ATTRIBUTE_CALLBACK callback, LPVOID param)
{
    SDP_ELEMENT_DATA element;
    if (!stream || !length || !callback || BluetoothSdpGetElementData(stream, length, &element) ||
        element.type != SDP_TYPE_SEQUENCE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BYTE *at = nullptr;
    for (;;) {
        ULONG left;
        at = next_element(stream, length, at, &left);
        if (!at)
            return TRUE;
        if (BluetoothSdpGetElementData(at, left, &element) || element.specificType != SDP_ST_UINT16) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        USHORT id = element.data.uint16;
        at = next_element(stream, length, at, &left);
        BYTE type, index;
        ULONG size, header;
        if (!at || !element_header(at, left, &type, &index, &size, &header)) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        if (!callback(id, at, header + size, param))
            return TRUE;
    }
}
