/*
 * hid.dll: HidD_ asks a collection's driver with its device controls,
 * HidP_ reads and writes reports through the preparsed data the parser
 * of the kernel lays out. see docs/hid.md.
 */
#define MIME_BUILD_HID
#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/win32/hidpi.h>

#define SIGNATURE0 0x50646948
#define SIGNATURE1 0x52444B20
#define HEADER_SIZE 44
#define CAP_SIZE 104
#define NODE_SIZE 16

enum {
    CAP_ARRAY_MORE = 0x1,
    CAP_CONSTANT = 0x2,
    CAP_BUTTON = 0x4,
    CAP_ABSOLUTE = 0x8,
    CAP_RANGE = 0x10,
    CAP_ALIAS = 0x20,
    CAP_STRING_RANGE = 0x40,
    CAP_DESIGNATOR_RANGE = 0x80,
};

#define IOCTL_HID_GET_DRIVER_CONFIG 0xB0190
#define IOCTL_HID_SET_FEATURE 0xB0191
#define IOCTL_HID_GET_FEATURE 0xB0192
#define IOCTL_HID_GET_COLLECTION_DESCRIPTOR 0xB0193
#define IOCTL_HID_SET_DRIVER_CONFIG 0xB0194
#define IOCTL_HID_SET_OUTPUT_REPORT 0xB0195
#define IOCTL_HID_FLUSH_QUEUE 0xB0197
#define IOCTL_GET_PHYSICAL_DESCRIPTOR 0xB019A
#define IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS 0xB01A0
#define IOCTL_HID_GET_INPUT_REPORT 0xB01A2
#define IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS 0xB01A4
#define IOCTL_HID_GET_COLLECTION_INFORMATION 0xB01A8
#define IOCTL_HID_GET_MANUFACTURER_STRING 0xB01BA
#define IOCTL_HID_GET_PRODUCT_STRING 0xB01BE
#define IOCTL_HID_GET_SERIALNUMBER_STRING 0xB01C2
#define IOCTL_HID_GET_INDEXED_STRING 0xB01E2
#define IOCTL_HID_GET_MS_GENRE_DESCRIPTOR 0xB01E6

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------ HidD */

/* a synchronous handle asks plainly; an overlapped one waits on an event that keeps the packet from its port. */
static BOOLEAN control(HANDLE device, ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len)
{
    IO_STATUS_BLOCK io;
    FILE_MODE_INFORMATION mode;
    NTSTATUS status = NtQueryInformationFile(device, &io, &mode, sizeof(mode), FileModeInformation);
    if (!NT_SUCCESS(status)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DWORD done;
    if (mode.Mode & FILE_SYNCHRONOUS_IO_NONALERT)
        return (BOOLEAN)DeviceIoControl(device, code, input, in_len, output, out_len, &done, nullptr);
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event)
        return FALSE;
    OVERLAPPED overlapped = {};
    overlapped.hEvent = (HANDLE)((ULONG_PTR)event | 1);
    BOOL ok = DeviceIoControl(device, code, input, in_len, output, out_len, &done, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(device, &overlapped, &done, TRUE);
    DWORD error = GetLastError();
    CloseHandle(event);
    SetLastError(error);
    return (BOOLEAN)ok;
}

HIDAPI void NTAPI HidD_GetHidGuid(GUID *guid)
{
    static const GUID HID = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    *guid = HID;
}

HIDAPI ULONG NTAPI HidD_Hello(PCHAR buffer, ULONG length)
{
    static const char HELLO[7] = "Hello\n";
    memcpy(buffer, HELLO, length < sizeof(HELLO) ? length : sizeof(HELLO));
    return sizeof(HELLO);
}

HIDAPI BOOLEAN NTAPI HidD_GetAttributes(HANDLE device, PHIDD_ATTRIBUTES attributes)
{
    UCHAR info[12];
    if (!control(device, IOCTL_HID_GET_COLLECTION_INFORMATION, nullptr, 0, info, sizeof(info)))
        return FALSE;
    attributes->Size = sizeof(*attributes);
    memcpy(&attributes->VendorID, info + 6, 6);
    return TRUE;
}

/* the blob follows a 16 byte prefix whose first dword names its allocator. */
static ULONG preparsed_tag(void)
{
    return (ULONG)(ULONG_PTR)HidD_Hello;
}

HIDAPI BOOLEAN NTAPI HidD_GetPreparsedData(HANDLE device, PHIDP_PREPARSED_DATA *data)
{
    UCHAR info[12];
    if (!control(device, IOCTL_HID_GET_COLLECTION_INFORMATION, nullptr, 0, info, sizeof(info)))
        return FALSE;
    ULONG size;
    memcpy(&size, info, sizeof(size));
    PUCHAR block = size > 0xFFFFFFEF ? nullptr : LocalAlloc(LPTR, size + 16);
    if (!block) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ULONG tag = preparsed_tag();
    memcpy(block, &tag, sizeof(tag));
    *data = (PHIDP_PREPARSED_DATA)(block + 16);
    return control(device, IOCTL_HID_GET_COLLECTION_DESCRIPTOR, nullptr, 0, block + 16, size);
}

HIDAPI BOOLEAN NTAPI HidD_FreePreparsedData(PHIDP_PREPARSED_DATA data)
{
    PUCHAR block = (PUCHAR)data - 16;
    ULONG tag;
    memcpy(&tag, block, sizeof(tag));
    if (tag != preparsed_tag())
        return FALSE;
    LocalFree(block);
    return TRUE;
}

HIDAPI BOOLEAN NTAPI HidD_FlushQueue(HANDLE device)
{
    return control(device, IOCTL_HID_FLUSH_QUEUE, nullptr, 0, nullptr, 0);
}

HIDAPI BOOLEAN NTAPI HidD_GetNumInputBuffers(HANDLE device, PULONG count)
{
    return control(device, IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS, nullptr, 0, count, sizeof(*count));
}

HIDAPI BOOLEAN NTAPI HidD_SetNumInputBuffers(HANDLE device, ULONG count)
{
    return control(device, IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS, &count, sizeof(count), nullptr, 0);
}

HIDAPI BOOLEAN NTAPI HidD_GetInputReport(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_GET_INPUT_REPORT, nullptr, 0, buffer, length);
}

HIDAPI BOOLEAN NTAPI HidD_SetOutputReport(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_SET_OUTPUT_REPORT, buffer, length, nullptr, 0);
}

HIDAPI BOOLEAN NTAPI HidD_GetFeature(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_GET_FEATURE, nullptr, 0, buffer, length);
}

HIDAPI BOOLEAN NTAPI HidD_SetFeature(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_SET_FEATURE, buffer, length, nullptr, 0);
}

HIDAPI BOOLEAN NTAPI HidD_GetManufacturerString(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_GET_MANUFACTURER_STRING, nullptr, 0, buffer, length);
}

HIDAPI BOOLEAN NTAPI HidD_GetProductString(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_GET_PRODUCT_STRING, nullptr, 0, buffer, length);
}

HIDAPI BOOLEAN NTAPI HidD_GetSerialNumberString(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_GET_SERIALNUMBER_STRING, nullptr, 0, buffer, length);
}

HIDAPI BOOLEAN NTAPI HidD_GetIndexedString(HANDLE device, ULONG index, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_GET_INDEXED_STRING, &index, sizeof(index), buffer, length);
}

HIDAPI BOOLEAN NTAPI HidD_GetPhysicalDescriptor(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_GET_PHYSICAL_DESCRIPTOR, nullptr, 0, buffer, length);
}

HIDAPI BOOLEAN NTAPI HidD_GetMsGenreDescriptor(HANDLE device, PVOID buffer, ULONG length)
{
    return control(device, IOCTL_HID_GET_MS_GENRE_DESCRIPTOR, nullptr, 0, buffer, length);
}

/* the driver configuration calls, which hidclass does not implement. */
HIDAPI BOOLEAN NTAPI HidD_GetConfiguration(HANDLE device, PHIDD_CONFIGURATION configuration, ULONG length)
{
    if (length < sizeof(*configuration)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    configuration->cookie = (PVOID)HidD_GetConfiguration;
    return control(device, IOCTL_HID_GET_DRIVER_CONFIG, nullptr, 0, &configuration->size, length - sizeof(PVOID));
}

HIDAPI BOOLEAN NTAPI HidD_SetConfiguration(HANDLE device, PHIDD_CONFIGURATION configuration, ULONG length)
{
    if (length < sizeof(*configuration) || configuration->cookie != (PVOID)HidD_GetConfiguration) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return control(device, IOCTL_HID_SET_DRIVER_CONFIG, nullptr, 0, &configuration->size, length - 4);
}

/* ------------------------------------------------------------ preparsed data */

typedef const UCHAR *BLOB;

static USHORT get16(const UCHAR *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static ULONG get32(const UCHAR *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}

static BOOL signed_ok(BLOB b)
{
    return b && get32(b) == SIGNATURE0 && get32(b + 4) == SIGNATURE1;
}

typedef struct CAP {
    USHORT Page;
    UCHAR ReportId;
    UCHAR BitPosition;
    USHORT BitSize;
    USHORT ReportCount;
    USHORT BytePosition;
    USHORT BitCount;
    ULONG BitField;
    USHORT Link, LinkPage, LinkUsage;
    ULONG Flags;
    USHORT UsageMin, UsageMax;
    USHORT DataMin, DataMax;
    BOOLEAN HasNull;
    LONG LogicalMin, LogicalMax, PhysicalMin, PhysicalMax;
    ULONG Units, UnitsExp;
    const UCHAR *Raw;
} CAP;

static CAP cap_at(BLOB b, ULONG index)
{
    const UCHAR *c = b + HEADER_SIZE + CAP_SIZE * index;
    CAP r = {};
    r.Raw = c;
    r.Page = get16(c);
    r.ReportId = c[2];
    r.BitPosition = c[3];
    r.BitSize = get16(c + 4);
    r.ReportCount = get16(c + 6);
    r.BytePosition = get16(c + 8);
    r.BitCount = get16(c + 10);
    r.BitField = get32(c + 12);
    r.Link = get16(c + 18);
    r.LinkPage = get16(c + 20);
    r.LinkUsage = get16(c + 22);
    r.Flags = get32(c + 24);
    r.UsageMin = get16(c + 60);
    r.UsageMax = get16(c + 62);
    r.DataMin = get16(c + 72);
    r.DataMax = get16(c + 74);
    if (r.Flags & CAP_BUTTON) {
        r.LogicalMin = (LONG)get32(c + 76);
        r.LogicalMax = (LONG)get32(c + 80);
    } else {
        r.HasNull = c[76];
        r.LogicalMin = (LONG)get32(c + 80);
        r.LogicalMax = (LONG)get32(c + 84);
        r.PhysicalMin = (LONG)get32(c + 88);
        r.PhysicalMax = (LONG)get32(c + 92);
    }
    r.Units = get32(c + 96);
    r.UnitsExp = get32(c + 100);
    return r;
}

typedef struct KIND {
    USHORT First, Count, Last, Length;
} KIND;

static BOOL kind_of(BLOB b, HIDP_REPORT_TYPE type, KIND *k)
{
    if ((ULONG)type > HidP_Feature)
        return FALSE;
    const UCHAR *p = b + 16 + 8 * type;
    k->First = get16(p);
    k->Count = get16(p + 2);
    k->Last = get16(p + 4);
    k->Length = get16(p + 6);
    return TRUE;
}

/* the checks most HidP_ calls start with: the blob, the type, then the report's length. */
static NTSTATUS begin(BLOB b, HIDP_REPORT_TYPE type, ULONG report_length, KIND *k)
{
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    if (!kind_of(b, type, k))
        return HIDP_STATUS_INVALID_REPORT_TYPE;
    if ((USHORT)report_length != k->Length)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    if (!k->Length)
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    return HIDP_STATUS_SUCCESS;
}

static BOOL link_matches(USHORT filter, USHORT link)
{
    if (!filter)
        return TRUE;
    if (filter == HIDP_LINK_COLLECTION_ROOT)
        return link == 0;
    return filter == link;
}

static BOOL usage_matches(const CAP *c, USAGE usage)
{
    if (c->Flags & CAP_RANGE)
        return c->UsageMin <= usage && usage <= c->UsageMax;
    return usage == c->UsageMin;
}

static BOOL id_matches(const CAP *c, const UCHAR *report)
{
    return report[0] == 0 || report[0] == c->ReportId;
}

static ULONG field_offset(const CAP *c)
{
    return 8u * c->BytePosition + c->BitPosition;
}

static ULONG read_bits(const UCHAR *report, ULONG length, ULONG offset, ULONG size)
{
    ULONG value = 0;
    for (ULONG i = 0; i < size && i < 32; i++) {
        ULONG bit = offset + i;
        if (bit / 8 < length && (report[bit / 8] >> (bit % 8) & 1))
            value |= 1u << i;
    }
    return value;
}

static void write_bits(PUCHAR report, ULONG length, ULONG offset, ULONG size, ULONG value)
{
    for (ULONG i = 0; i < size; i++) {
        ULONG bit = offset + i;
        if (bit / 8 >= length)
            continue;
        UCHAR mask = (UCHAR)(1u << (bit % 8));
        if (i < 32 && (value >> i & 1))
            report[bit / 8] |= mask;
        else
            report[bit / 8] &= (UCHAR)~mask;
    }
}

/* an array's caps run from those marked as more of it to the one of its first usage, stored last. */
static ULONG run_end(BLOB b, const KIND *k, ULONG index)
{
    while (index + 1 < k->Last && (cap_at(b, index).Flags & CAP_ARRAY_MORE))
        index++;
    return index;
}

/* an array slot's value is a one based index into the usages in declaration order. */
static BOOL array_usage(BLOB b, ULONG first, ULONG last, ULONG value, CAP *found, USAGE *usage)
{
    for (ULONG i = last + 1; i-- > first;) {
        CAP c = cap_at(b, i);
        if (c.Flags & CAP_RANGE) {
            ULONG min = c.UsageMin ? c.UsageMin : 1, span = c.UsageMax - min + 1;
            if (value <= span) {
                *usage = (USAGE)(min - 1 + value);
                *found = c;
                return TRUE;
            }
            value -= span;
        } else {
            if (value == 1) {
                *usage = c.UsageMin;
                *found = c;
                return TRUE;
            }
            value--;
        }
    }
    return FALSE;
}

/* the inverse: where a usage sits among the array's usages, 0 when it is not one. */
static ULONG array_index(BLOB b, ULONG first, ULONG last, USAGE usage)
{
    ULONG index = 0;
    for (ULONG i = last + 1; i-- > first;) {
        CAP c = cap_at(b, i);
        if (c.Flags & CAP_RANGE) {
            ULONG min = c.UsageMin ? c.UsageMin : 1;
            if (usage >= min && usage <= c.UsageMax)
                return index + usage - min + 1;
            index += c.UsageMax - min + 1;
        } else {
            if (usage == c.UsageMin)
                return index + 1;
            index++;
        }
    }
    return 0;
}

HIDAPI NTSTATUS NTAPI HidP_GetCaps(PHIDP_PREPARSED_DATA data, PHIDP_CAPS caps)
{
    BLOB b = (BLOB)data;
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    memset(caps, 0, sizeof(*caps));
    caps->Usage = get16(b + 8);
    caps->UsagePage = get16(b + 10);
    caps->NumberLinkCollectionNodes = get16(b + 42);
    USHORT *lengths[3] = {&caps->InputReportByteLength, &caps->OutputReportByteLength, &caps->FeatureReportByteLength};
    USHORT *counts[3][3] = {
        {&caps->NumberInputButtonCaps, &caps->NumberInputValueCaps, &caps->NumberInputDataIndices},
        {&caps->NumberOutputButtonCaps, &caps->NumberOutputValueCaps, &caps->NumberOutputDataIndices},
        {&caps->NumberFeatureButtonCaps, &caps->NumberFeatureValueCaps, &caps->NumberFeatureDataIndices},
    };
    for (ULONG type = 0; type < 3; type++) {
        KIND k;
        kind_of(b, (HIDP_REPORT_TYPE)type, &k);
        *lengths[type] = k.Length;
        for (ULONG i = k.First; i < k.Last; i++) {
            CAP c = cap_at(b, i);
            (*counts[type][c.Flags & CAP_BUTTON ? 0 : 1])++;
            *counts[type][2] += (USHORT)(c.DataMax - c.DataMin + 1);
        }
    }
    return HIDP_STATUS_SUCCESS;
}

/* both caps queries: what they fill and how they count. */
static NTSTATUS query_caps(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage, BOOL buttons, PVOID out,
                           PUSHORT length, BLOB b)
{
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    KIND k;
    if (!kind_of(b, type, &k))
        return HIDP_STATUS_INVALID_REPORT_TYPE;
    ULONG count = 0;
    BOOL short_list = FALSE;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if (!(c.Flags & CAP_BUTTON) != !buttons)
            continue;
        if ((page && c.Page != page) || !link_matches(link, c.Link) || (usage && !usage_matches(&c, usage)))
            continue;
        if (count < *length) {
            USHORT reports = c.Flags & CAP_RANGE ? 1 : c.ReportCount;
            if (buttons) {
                HIDP_BUTTON_CAPS *o = (HIDP_BUTTON_CAPS *)out + count;
                o->UsagePage = c.Page;
                o->ReportID = c.ReportId;
                o->IsAlias = (c.Flags & CAP_ALIAS) != 0;
                o->BitField = (USHORT)c.BitField;
                o->LinkCollection = c.Link;
                o->LinkUsage = c.LinkUsage;
                o->LinkUsagePage = c.LinkPage;
                o->IsRange = (c.Flags & CAP_RANGE) != 0;
                o->IsStringRange = (c.Flags & CAP_STRING_RANGE) != 0;
                o->IsDesignatorRange = (c.Flags & CAP_DESIGNATOR_RANGE) != 0;
                o->IsAbsolute = (c.Flags & CAP_ABSOLUTE) != 0;
                o->ReportCount = reports;
                o->Reserved2 = 0;
                memcpy(&o->Range, c.Raw + 60, 16);
            } else {
                HIDP_VALUE_CAPS *o = (HIDP_VALUE_CAPS *)out + count;
                o->UsagePage = c.Page;
                o->LinkCollection = c.Link;
                o->LinkUsagePage = c.LinkPage;
                o->LinkUsage = c.LinkUsage;
                o->IsRange = (c.Flags & CAP_RANGE) != 0;
                o->IsStringRange = (c.Flags & CAP_STRING_RANGE) != 0;
                o->IsDesignatorRange = (c.Flags & CAP_DESIGNATOR_RANGE) != 0;
                o->ReportID = c.ReportId;
                o->BitField = (USHORT)c.BitField;
                o->BitSize = c.BitSize;
                o->IsAbsolute = (c.Flags & CAP_ABSOLUTE) != 0;
                o->HasNull = c.HasNull;
                o->Units = c.Units;
                o->UnitsExp = c.UnitsExp;
                o->LogicalMin = c.LogicalMin;
                o->LogicalMax = c.LogicalMax;
                o->PhysicalMin = c.PhysicalMin;
                o->PhysicalMax = c.PhysicalMax;
                o->IsAlias = (c.Flags & CAP_ALIAS) != 0;
                o->ReportCount = reports;
                memcpy(&o->Range, c.Raw + 60, 16);
            }
        } else {
            short_list = TRUE;
        }
        count++;
    }
    *length = (USHORT)count;
    if (!count)
        return HIDP_STATUS_USAGE_NOT_FOUND;
    return short_list ? HIDP_STATUS_BUFFER_TOO_SMALL : HIDP_STATUS_SUCCESS;
}

HIDAPI NTSTATUS NTAPI HidP_GetSpecificButtonCaps(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                                 PHIDP_BUTTON_CAPS caps, PUSHORT length, PHIDP_PREPARSED_DATA data)
{
    return query_caps(type, page, link, usage, TRUE, caps, length, (BLOB)data);
}

HIDAPI NTSTATUS NTAPI HidP_GetButtonCaps(HIDP_REPORT_TYPE type, PHIDP_BUTTON_CAPS caps, PUSHORT length,
                                         PHIDP_PREPARSED_DATA data)
{
    return query_caps(type, 0, 0, 0, TRUE, caps, length, (BLOB)data);
}

HIDAPI NTSTATUS NTAPI HidP_GetSpecificValueCaps(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                                PHIDP_VALUE_CAPS caps, PUSHORT length, PHIDP_PREPARSED_DATA data)
{
    return query_caps(type, page, link, usage, FALSE, caps, length, (BLOB)data);
}

HIDAPI NTSTATUS NTAPI HidP_GetValueCaps(HIDP_REPORT_TYPE type, PHIDP_VALUE_CAPS caps, PUSHORT length,
                                        PHIDP_PREPARSED_DATA data)
{
    return query_caps(type, 0, 0, 0, FALSE, caps, length, (BLOB)data);
}

HIDAPI NTSTATUS NTAPI HidP_GetLinkCollectionNodes(PHIDP_LINK_COLLECTION_NODE nodes, PULONG length,
                                                  PHIDP_PREPARSED_DATA data)
{
    BLOB b = (BLOB)data;
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    ULONG count = get16(b + 42), room = *length;
    const UCHAR *first = b + HEADER_SIZE + get16(b + 40);
    *length = count;
    for (ULONG i = 0; i < count && i < room; i++) {
        const UCHAR *n = first + NODE_SIZE * i;
        HIDP_LINK_COLLECTION_NODE *o = &nodes[i];
        o->LinkUsage = get16(n);
        o->LinkUsagePage = get16(n + 2);
        o->Parent = get16(n + 4);
        o->NumberOfChildren = get16(n + 6);
        o->NextSibling = get16(n + 8);
        o->FirstChild = get16(n + 10);
        ULONG bits = get32(n + 12);
        o->CollectionType = bits & 0xFF;
        o->IsAlias = bits >> 8 & 1;
    }
    return room >= count ? HIDP_STATUS_SUCCESS : HIDP_STATUS_BUFFER_TOO_SMALL;
}

/* the signature check of these two passes when either dword is right. */
static BOOL loosely_signed(BLOB b)
{
    return get32(b) == SIGNATURE0 || get32(b + 4) == SIGNATURE1;
}

HIDAPI ULONG NTAPI HidP_MaxUsageListLength(HIDP_REPORT_TYPE type, USAGE page, PHIDP_PREPARSED_DATA data)
{
    BLOB b = (BLOB)data;
    KIND k;
    if (!loosely_signed(b) || !kind_of(b, type, &k))
        return 0;
    ULONG total = 0;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if ((c.Flags & CAP_BUTTON) && (!page || c.Page == page))
            total += c.ReportCount;
    }
    return total;
}

HIDAPI ULONG NTAPI HidP_MaxDataListLength(HIDP_REPORT_TYPE type, PHIDP_PREPARSED_DATA data)
{
    BLOB b = (BLOB)data;
    KIND k;
    if (!loosely_signed(b) || !kind_of(b, type, &k))
        return 0;
    ULONG total = 0;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        total += (c.Flags & (CAP_BUTTON | CAP_RANGE)) ? c.ReportCount : 1;
    }
    return total;
}

/* HidP_GetUsages and its Ex form, which reports the page of each usage too. */
static NTSTATUS get_usages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PVOID list, BOOL pages, PULONG length,
                           BLOB b, const UCHAR *report, ULONG report_length)
{
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    memset(list, 0, *length * sizeof(USAGE));
    KIND k;
    NTSTATUS status = begin(b, type, report_length, &k);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    ULONG count = 0;
    BOOL matched = FALSE, wrong_id = FALSE;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if (!(c.Flags & CAP_BUTTON) || (page && c.Page != page) || !link_matches(link, c.Link))
            continue;
        if (!id_matches(&c, report)) {
            wrong_id = TRUE;
            continue;
        }
        matched = TRUE;
        ULONG offset = field_offset(&c);
        if (c.BitSize == 1) {
            for (ULONG bit = 0; bit < c.BitCount; bit++) {
                if (!read_bits(report, report_length, offset + bit, 1))
                    continue;
                USAGE usage = c.Flags & CAP_RANGE ? (USAGE)(c.UsageMin + bit) : c.UsageMin;
                if (usage && count < *length) {
                    if (pages)
                        ((USAGE_AND_PAGE *)list)[count] = (USAGE_AND_PAGE){usage, c.Page};
                    else
                        ((PUSAGE)list)[count] = usage;
                }
                count += usage != 0;
            }
            continue;
        }
        ULONG last = run_end(b, &k, i);
        for (ULONG slot = 0; slot < c.ReportCount; slot++) {
            ULONG value = read_bits(report, report_length, offset + slot * c.BitSize, c.BitSize);
            CAP owner;
            USAGE usage;
            if (!value || !array_usage(b, i, last, value, &owner, &usage) || !usage)
                continue;
            if (count < *length) {
                if (pages)
                    ((USAGE_AND_PAGE *)list)[count] = (USAGE_AND_PAGE){usage, owner.Page};
                else
                    ((PUSAGE)list)[count] = usage;
            }
            count++;
        }
        i = last;
    }
    ULONG room = *length;
    *length = count;
    if (!matched)
        return wrong_id ? HIDP_STATUS_INCOMPATIBLE_REPORT_ID : HIDP_STATUS_USAGE_NOT_FOUND;
    return room < count ? HIDP_STATUS_BUFFER_TOO_SMALL : HIDP_STATUS_SUCCESS;
}

HIDAPI NTSTATUS NTAPI HidP_GetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list, PULONG length,
                                     PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length)
{
    return get_usages(type, page, link, list, page == 0, length, (BLOB)data, (const UCHAR *)report, report_length);
}

HIDAPI NTSTATUS NTAPI HidP_GetUsagesEx(HIDP_REPORT_TYPE type, USHORT link, PUSAGE_AND_PAGE list, PULONG length,
                                       PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length)
{
    return get_usages(type, 0, link, list, TRUE, length, (BLOB)data, (const UCHAR *)report, report_length);
}

/* sets or clears one usage of a button cap. */
static NTSTATUS set_usage(BLOB b, const KIND *k, USAGE page, USHORT link, USAGE usage, BOOL set, PUCHAR report,
                          ULONG report_length)
{
    if (!report_length)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    BOOL wrong_id = FALSE, not_pressed = FALSE, full = FALSE;
    for (ULONG i = k->First; i < k->Last; i++) {
        CAP c = cap_at(b, i);
        if (!(c.Flags & CAP_BUTTON) || c.Page != page || !link_matches(link, c.Link))
            continue;
        ULONG last = c.BitSize == 1 ? i : run_end(b, k, i);
        if (c.BitSize == 1) {
            if (!usage_matches(&c, usage))
                continue;
        } else if (!array_index(b, i, last, usage)) {
            i = last;
            continue;
        }
        if (!id_matches(&c, report)) {
            wrong_id = TRUE;
            i = last;
            continue;
        }
        report[0] = c.ReportId;
        ULONG offset = field_offset(&c);
        if (c.BitSize == 1) {
            ULONG bit = offset + (c.Flags & CAP_RANGE ? usage - c.UsageMin : 0);
            if (bit / 8 >= report_length)
                return HIDP_STATUS_INVALID_REPORT_LENGTH;
            UCHAR mask = (UCHAR)(1u << (bit % 8));
            if (set) {
                report[bit / 8] |= mask;
                return HIDP_STATUS_SUCCESS;
            }
            if (!(report[bit / 8] & mask))
                return HIDP_STATUS_BUTTON_NOT_PRESSED;
            report[bit / 8] &= (UCHAR)~mask;
            return HIDP_STATUS_SUCCESS;
        }
        ULONG index = array_index(b, i, last, usage);
        if (!index)
            return HIDP_STATUS_INTERNAL_ERROR;
        for (ULONG slot = 0; slot < c.ReportCount; slot++) {
            ULONG at = offset + slot * c.BitSize;
            ULONG value = read_bits(report, report_length, at, c.BitSize);
            if (set && !value) {
                write_bits(report, report_length, at, c.BitSize, index);
                return HIDP_STATUS_SUCCESS;
            }
            if (!set && value == index) {
                for (ULONG next = slot + 1; next < c.ReportCount; next++)
                    write_bits(report, report_length, offset + (next - 1) * c.BitSize, c.BitSize,
                               read_bits(report, report_length, offset + next * c.BitSize, c.BitSize));
                write_bits(report, report_length, offset + (c.ReportCount - 1) * c.BitSize, c.BitSize, 0);
                return HIDP_STATUS_SUCCESS;
            }
        }
        if (set)
            full = TRUE;
        else
            not_pressed = TRUE;
        i = last;
    }
    if (wrong_id)
        return HIDP_STATUS_INCOMPATIBLE_REPORT_ID;
    if (not_pressed)
        return HIDP_STATUS_BUTTON_NOT_PRESSED;
    if (full)
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    return HIDP_STATUS_USAGE_NOT_FOUND;
}

static NTSTATUS set_usages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list, PULONG length, BLOB b,
                           PUCHAR report, ULONG report_length, BOOL set)
{
    KIND k;
    NTSTATUS status = begin(b, type, report_length, &k);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    for (ULONG i = 0; i < *length; i++) {
        if (!list[i])
            continue;
        status = set_usage(b, &k, page, link, list[i], set, report, report_length);
        if (status != HIDP_STATUS_SUCCESS) {
            *length = i;
            return status;
        }
    }
    return status;
}

HIDAPI NTSTATUS NTAPI HidP_SetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list, PULONG length,
                                     PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length)
{
    return set_usages(type, page, link, list, length, (BLOB)data, (PUCHAR)report, report_length, TRUE);
}

HIDAPI NTSTATUS NTAPI HidP_UnsetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list, PULONG length,
                                       PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length)
{
    return set_usages(type, page, link, list, length, (BLOB)data, (PUCHAR)report, report_length, FALSE);
}

/* the button arrays of the two calls: a cap of one usage and several fields, one bit each. */
static NTSTATUS find_button_array(BLOB b, HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                  const UCHAR *report, ULONG report_length, CAP *found)
{
    KIND k;
    if (!kind_of(b, type, &k))
        return HIDP_STATUS_INVALID_REPORT_TYPE;
    if (!k.Length)
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    if ((USHORT)report_length != k.Length)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    BOOL wrong_id = FALSE;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if (!(c.Flags & CAP_BUTTON) || (page && c.Page != page) || !link_matches(link, c.Link))
            continue;
        if ((c.Flags & CAP_RANGE) ? usage_matches(&c, usage) : (c.UsageMin == usage && c.ReportCount == 1))
            return HIDP_STATUS_NOT_BUTTON_ARRAY;
        if ((c.Flags & CAP_RANGE) || c.UsageMin != usage)
            continue;
        if (!id_matches(&c, report)) {
            wrong_id = TRUE;
            continue;
        }
        *found = c;
        return HIDP_STATUS_SUCCESS;
    }
    return wrong_id ? HIDP_STATUS_INCOMPATIBLE_REPORT_ID : HIDP_STATUS_USAGE_NOT_FOUND;
}

HIDAPI NTSTATUS NTAPI HidP_GetButtonArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                          PHIDP_BUTTON_ARRAY_DATA list, PUSHORT length, PHIDP_PREPARSED_DATA data,
                                          PCHAR report, ULONG report_length)
{
    BLOB b = (BLOB)data;
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    memset(list, 0, *length * sizeof(*list));
    CAP c;
    NTSTATUS status = find_button_array(b, type, page, link, usage, (const UCHAR *)report, report_length, &c);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    ULONG count = 0, room = *length;
    for (ULONG k = 0; k < c.ReportCount; k++) {
        if (!read_bits((const UCHAR *)report, report_length, field_offset(&c) + k, 1))
            continue;
        if (count < room)
            list[count] = (HIDP_BUTTON_ARRAY_DATA){(USHORT)k, TRUE};
        count++;
    }
    *length = (USHORT)count;
    return count > room ? HIDP_STATUS_BUFFER_TOO_SMALL : HIDP_STATUS_SUCCESS;
}

HIDAPI NTSTATUS NTAPI HidP_SetButtonArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                          PHIDP_BUTTON_ARRAY_DATA list, USHORT length, PHIDP_PREPARSED_DATA data,
                                          PCHAR report, ULONG report_length)
{
    BLOB b = (BLOB)data;
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    CAP c;
    NTSTATUS status = find_button_array(b, type, page, link, usage, (const UCHAR *)report, report_length, &c);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    for (ULONG i = 0; i < length; i++) {
        if (list[i].ArrayIndex > c.ReportCount)
            return HIDP_STATUS_DATA_INDEX_OUT_OF_RANGE;
        ULONG bit = field_offset(&c) + list[i].ArrayIndex;
        if (bit / 8 >= report_length)
            return HIDP_STATUS_INVALID_REPORT_LENGTH;
        write_bits((PUCHAR)report, report_length, bit, 1, list[i].On ? 1 : 0);
    }
    return HIDP_STATUS_SUCCESS;
}

/* the value cap of a usage and the bit offset of its field. */
static NTSTATUS find_value(BLOB b, HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage, const UCHAR *report,
                           ULONG report_length, CAP *found, ULONG *offset)
{
    KIND k;
    NTSTATUS status = begin(b, type, report_length, &k);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    BOOL wrong_id = FALSE;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if ((c.Flags & CAP_BUTTON) || c.Page != page || !link_matches(link, c.Link) || !usage_matches(&c, usage))
            continue;
        if (!id_matches(&c, report)) {
            wrong_id = TRUE;
            continue;
        }
        *found = c;
        *offset = field_offset(&c) + (c.Flags & CAP_RANGE ? (usage - c.UsageMin) * c.BitSize : 0);
        return HIDP_STATUS_SUCCESS;
    }
    return wrong_id ? HIDP_STATUS_INCOMPATIBLE_REPORT_ID : HIDP_STATUS_USAGE_NOT_FOUND;
}

HIDAPI NTSTATUS NTAPI HidP_GetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage, PULONG value,
                                         PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length)
{
    CAP c;
    ULONG offset;
    NTSTATUS status =
        find_value((BLOB)data, type, page, link, usage, (const UCHAR *)report, report_length, &c, &offset);
    if (status == HIDP_STATUS_SUCCESS)
        *value = read_bits((const UCHAR *)report, report_length, offset, c.BitSize);
    return status;
}

static LONG sign_extend(ULONG raw, ULONG bits)
{
    if (!bits || bits >= 32)
        return (LONG)raw;
    ULONG sign = 1u << (bits - 1);
    return (LONG)((raw ^ sign) - sign);
}

HIDAPI NTSTATUS NTAPI HidP_GetScaledUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                               PLONG value, PHIDP_PREPARSED_DATA data, PCHAR report,
                                               ULONG report_length)
{
    CAP c;
    ULONG offset;
    NTSTATUS status =
        find_value((BLOB)data, type, page, link, usage, (const UCHAR *)report, report_length, &c, &offset);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    LONG raw = sign_extend(read_bits((const UCHAR *)report, report_length, offset, c.BitSize), c.BitSize);
    LONG scaled;
    if (c.PhysicalMin || c.PhysicalMax) {
        if (!(c.LogicalMax > c.LogicalMin && c.PhysicalMax > c.PhysicalMin)) {
            *value = 0;
            return HIDP_STATUS_BAD_LOG_PHY_VALUES;
        }
        scaled = (LONG)(c.PhysicalMin + ((LONG64)raw - c.LogicalMin) * ((LONG64)c.PhysicalMax - c.PhysicalMin) /
                                            ((LONG64)c.LogicalMax - c.LogicalMin));
    } else {
        if (c.LogicalMax == c.LogicalMin) {
            *value = 0;
            return HIDP_STATUS_BAD_LOG_PHY_VALUES;
        }
        scaled = raw;
    }
    *value = scaled;
    if (c.LogicalMin <= raw && raw <= c.LogicalMax)
        return HIDP_STATUS_SUCCESS;
    *value = 0;
    return c.HasNull ? HIDP_STATUS_NULL : HIDP_STATUS_VALUE_OUT_OF_RANGE;
}

HIDAPI NTSTATUS NTAPI HidP_SetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage, ULONG value,
                                         PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length)
{
    CAP c;
    ULONG offset;
    NTSTATUS status =
        find_value((BLOB)data, type, page, link, usage, (const UCHAR *)report, report_length, &c, &offset);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    report[0] = (CHAR)c.ReportId;
    write_bits((PUCHAR)report, report_length, offset, c.BitSize, value);
    return HIDP_STATUS_SUCCESS;
}

HIDAPI NTSTATUS NTAPI HidP_SetScaledUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                               LONG value, PHIDP_PREPARSED_DATA data, PCHAR report,
                                               ULONG report_length)
{
    CAP c;
    ULONG offset;
    NTSTATUS status =
        find_value((BLOB)data, type, page, link, usage, (const UCHAR *)report, report_length, &c, &offset);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    report[0] = (CHAR)c.ReportId;
    ULONG top = c.BitSize ? 1u << ((c.BitSize - 1) & 31) : 0;
    ULONG raw;
    if (c.PhysicalMin || c.PhysicalMax) {
        if (!(c.LogicalMax > c.LogicalMin && c.PhysicalMax > c.PhysicalMin))
            return HIDP_STATUS_BAD_LOG_PHY_VALUES;
        if (c.PhysicalMin <= value && value <= c.PhysicalMax)
            raw = (ULONG)(c.LogicalMin + (value - c.PhysicalMin) * (c.LogicalMax - c.LogicalMin + 1) /
                                             (c.PhysicalMax - c.PhysicalMin + 1));
        else if (c.HasNull)
            raw = top;
        else
            return HIDP_STATUS_VALUE_OUT_OF_RANGE;
    } else {
        if (c.LogicalMax == c.LogicalMin)
            return HIDP_STATUS_BAD_LOG_PHY_VALUES;
        if (c.LogicalMin <= value && value <= c.LogicalMax)
            raw = value >= 0 ? (ULONG)value & (top - 1) : (ULONG)value | top;
        else if (c.HasNull)
            raw = top;
        else
            return HIDP_STATUS_VALUE_OUT_OF_RANGE;
    }
    write_bits((PUCHAR)report, report_length, offset, c.BitSize, raw);
    return HIDP_STATUS_SUCCESS;
}

/* a value cap of one usage and several fields, read or written byte by byte. */
static NTSTATUS value_array(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage, PCHAR buffer,
                            USHORT buffer_length, BLOB b, PCHAR report, ULONG report_length, BOOL set)
{
    KIND k;
    NTSTATUS status = begin(b, type, report_length, &k);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    BOOL wrong_id = FALSE;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if ((c.Flags & CAP_BUTTON) || c.Page != page || !link_matches(link, c.Link))
            continue;
        if ((c.Flags & CAP_RANGE) ? usage_matches(&c, usage) : (c.UsageMin == usage && c.ReportCount == 1))
            return HIDP_STATUS_NOT_VALUE_ARRAY;
        if ((c.Flags & CAP_RANGE) || c.UsageMin != usage)
            continue;
        if (!id_matches(&c, (const UCHAR *)report)) {
            wrong_id = TRUE;
            continue;
        }
        if (8u * buffer_length < (ULONG)c.ReportCount * c.BitSize)
            return HIDP_STATUS_BUFFER_TOO_SMALL;
        if (c.BitSize % 8)
            return HIDP_STATUS_NOT_IMPLEMENTED;
        ULONG bytes = (ULONG)c.ReportCount * c.BitSize / 8, offset = field_offset(&c);
        if (set)
            report[0] = (CHAR)c.ReportId;
        for (ULONG n = 0; n < bytes; n++) {
            if (set)
                write_bits((PUCHAR)report, report_length, offset + 8 * n, 8, (UCHAR)buffer[n]);
            else
                buffer[n] = (CHAR)read_bits((const UCHAR *)report, report_length, offset + 8 * n, 8);
        }
        return HIDP_STATUS_SUCCESS;
    }
    return wrong_id ? HIDP_STATUS_INCOMPATIBLE_REPORT_ID : HIDP_STATUS_USAGE_NOT_FOUND;
}

HIDAPI NTSTATUS NTAPI HidP_GetUsageValueArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                              PCHAR buffer, USHORT buffer_length, PHIDP_PREPARSED_DATA data,
                                              PCHAR report, ULONG report_length)
{
    return value_array(type, page, link, usage, buffer, buffer_length, (BLOB)data, report, report_length, FALSE);
}

HIDAPI NTSTATUS NTAPI HidP_SetUsageValueArray(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                              PCHAR buffer, USHORT buffer_length, PHIDP_PREPARSED_DATA data,
                                              PCHAR report, ULONG report_length)
{
    return value_array(type, page, link, usage, buffer, buffer_length, (BLOB)data, report, report_length, TRUE);
}

HIDAPI NTSTATUS NTAPI HidP_GetData(HIDP_REPORT_TYPE type, PHIDP_DATA list, PULONG length, PHIDP_PREPARSED_DATA data,
                                   PCHAR report, ULONG report_length)
{
    BLOB b = (BLOB)data;
    KIND k;
    NTSTATUS status = begin(b, type, report_length, &k);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    const UCHAR *r = (const UCHAR *)report;
    ULONG count = 0, room = *length;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if ((!(c.Flags & CAP_RANGE) && c.ReportCount != 1) || !id_matches(&c, r))
            continue;
        ULONG offset = field_offset(&c);
        if (!(c.Flags & CAP_BUTTON)) {
            for (ULONG f = 0; f < c.ReportCount; f++) {
                if (count < room)
                    list[count] = (HIDP_DATA){.DataIndex = (USHORT)(c.DataMin + f),
                                              .RawValue = read_bits(r, report_length, offset + f * c.BitSize,
                                                                    c.BitSize)};
                count++;
            }
        } else if (c.BitSize == 1) {
            for (ULONG bit = 0; bit < c.ReportCount; bit++) {
                if (!read_bits(r, report_length, offset + bit, 1))
                    continue;
                if (count < room) {
                    list[count].DataIndex = (USHORT)(c.DataMin + (c.Flags & CAP_RANGE ? bit : 0));
                    list[count].On = TRUE;
                }
                count++;
            }
        } else {
            ULONG last = run_end(b, &k, i);
            CAP end = cap_at(b, last);
            for (ULONG slot = 0; slot < c.ReportCount; slot++) {
                ULONG value = read_bits(r, report_length, offset + slot * c.BitSize, c.BitSize);
                if (!value)
                    continue;
                if (count < room) {
                    list[count].DataIndex = (USHORT)(end.DataMin + value - (end.UsageMin ? 1 : 0));
                    list[count].On = TRUE;
                }
                count++;
            }
            i = last;
        }
    }
    *length = count;
    return room >= count ? HIDP_STATUS_SUCCESS : HIDP_STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS set_datum(BLOB b, const KIND *k, const HIDP_DATA *d, PUCHAR report, ULONG report_length)
{
    if (!report_length)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    BOOL wrong_id = FALSE, not_pressed = FALSE, full = FALSE;
    for (ULONG i = k->First; i < k->Last; i++) {
        CAP c = cap_at(b, i);
        if (d->DataIndex < c.DataMin || d->DataIndex > c.DataMax)
            continue;
        if (!(c.Flags & CAP_RANGE) && c.ReportCount > 1 && !(c.Flags & CAP_BUTTON && c.BitSize > 1))
            return HIDP_STATUS_IS_VALUE_ARRAY;
        if (!id_matches(&c, report)) {
            wrong_id = TRUE;
            continue;
        }
        if (!report[0])
            report[0] = c.ReportId;
        ULONG offset = field_offset(&c), index = d->DataIndex - c.DataMin;
        if (!(c.Flags & CAP_BUTTON)) {
            write_bits(report, report_length, offset + index * c.BitSize, c.BitSize, d->RawValue);
            return HIDP_STATUS_SUCCESS;
        }
        if (c.BitSize == 1) {
            ULONG bit = offset + index;
            if (bit / 8 >= report_length)
                return HIDP_STATUS_INVALID_REPORT_LENGTH;
            if (d->On) {
                write_bits(report, report_length, bit, 1, 1);
                return HIDP_STATUS_SUCCESS;
            }
            if (read_bits(report, report_length, bit, 1)) {
                write_bits(report, report_length, bit, 1, 0);
                return HIDP_STATUS_SUCCESS;
            }
            not_pressed = TRUE;
            continue;
        }
        /* the run this cap ends, back to where it starts. */
        ULONG first = i;
        while (first > k->First && (cap_at(b, first - 1).Flags & CAP_ARRAY_MORE))
            first--;
        CAP head = cap_at(b, first);
        ULONG value = d->DataIndex - c.DataMin + (c.UsageMin ? 1 : 0), base = field_offset(&head);
        for (ULONG slot = 0; slot < head.ReportCount; slot++) {
            ULONG at = base + slot * head.BitSize, current = read_bits(report, report_length, at, head.BitSize);
            if (d->On && !current) {
                write_bits(report, report_length, at, head.BitSize, value);
                return HIDP_STATUS_SUCCESS;
            }
            if (!d->On && current == value) {
                for (ULONG next = slot + 1; next < head.ReportCount; next++)
                    write_bits(report, report_length, base + (next - 1) * head.BitSize, head.BitSize,
                               read_bits(report, report_length, base + next * head.BitSize, head.BitSize));
                write_bits(report, report_length, base + (head.ReportCount - 1) * head.BitSize, head.BitSize, 0);
                return HIDP_STATUS_SUCCESS;
            }
        }
        if (d->On)
            full = TRUE;
        else
            not_pressed = TRUE;
    }
    if (wrong_id)
        return HIDP_STATUS_INCOMPATIBLE_REPORT_ID;
    if (not_pressed)
        return HIDP_STATUS_BUTTON_NOT_PRESSED;
    if (full)
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    return HIDP_STATUS_DATA_INDEX_NOT_FOUND;
}

HIDAPI NTSTATUS NTAPI HidP_SetData(HIDP_REPORT_TYPE type, PHIDP_DATA list, PULONG length, PHIDP_PREPARSED_DATA data,
                                   PCHAR report, ULONG report_length)
{
    BLOB b = (BLOB)data;
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    KIND k;
    if (!kind_of(b, type, &k))
        return HIDP_STATUS_INVALID_REPORT_TYPE;
    if ((USHORT)report_length != k.Length)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    NTSTATUS status = HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    if (!k.Length)
        return status;
    ULONG applied = 0;
    for (; applied < *length; applied++) {
        status = set_datum(b, &k, &list[applied], (PUCHAR)report, report_length);
        if (status != HIDP_STATUS_SUCCESS)
            break;
    }
    *length = applied;
    return status;
}

HIDAPI NTSTATUS NTAPI HidP_InitializeReportForID(HIDP_REPORT_TYPE type, UCHAR id, PHIDP_PREPARSED_DATA data,
                                                 PCHAR report, ULONG report_length)
{
    BLOB b = (BLOB)data;
    KIND k;
    NTSTATUS status = begin(b, type, report_length, &k);
    if (status != HIDP_STATUS_SUCCESS)
        return status;
    memset(report, 0, report_length);
    report[0] = (CHAR)id;
    status = HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if (c.ReportId != id)
            continue;
        status = HIDP_STATUS_SUCCESS;
        if ((c.Flags & (CAP_BUTTON | CAP_CONSTANT | CAP_ALIAS)) || !c.HasNull)
            continue;
        /* the null value is the one just below the logical range, when that lies outside it. */
        ULONG mask = c.BitSize >= 32 ? 0xFFFFFFFF : (1u << c.BitSize) - 1;
        ULONG null = c.LogicalMax >= c.LogicalMin ? (ULONG)(c.LogicalMin - 1) & mask : 0;
        if (!null || ((LONG)null >= c.LogicalMin && (LONG)null <= c.LogicalMax))
            continue;
        ULONG fields = c.Flags & CAP_RANGE ? c.ReportCount : 1;
        for (ULONG f = 0; f < fields; f++)
            write_bits((PUCHAR)report, report_length, field_offset(&c) + f * c.BitSize, c.BitSize, null);
    }
    return status;
}

static BOOL listed(const USAGE *list, ULONG length, USAGE usage)
{
    for (ULONG i = 0; i < length && list[i]; i++)
        if (list[i] == usage)
            return TRUE;
    return FALSE;
}

HIDAPI NTSTATUS NTAPI HidP_UsageListDifference(PUSAGE previous, PUSAGE current, PUSAGE broken, PUSAGE made,
                                               ULONG length)
{
    ULONG b = 0, m = 0;
    for (ULONG i = 0; i < length && previous[i]; i++)
        if (!listed(current, length, previous[i]))
            broken[b++] = previous[i];
    for (ULONG i = 0; i < length && current[i]; i++)
        if (!listed(previous, length, current[i]))
            made[m++] = current[i];
    while (b < length)
        broken[b++] = 0;
    while (m < length)
        made[m++] = 0;
    return HIDP_STATUS_SUCCESS;
}

/* ------------------------------------------------------------ keyboard usages to scan codes */

enum { NAV = 0xF0, MOD = 0xF1, LANG = 0xF2, PRINT = 0xF3, UNKNOWN = 0 };
#define SPECIAL(kind, index) ((kind) | (index) << 8)

/* each entry emits its bytes from the low one up to the first zero; none means no translation. */
static const ULONG g_scancodes[0x100] = {
    [0x04] = 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,
    [0x14] = 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    [0x24] = 0x08, 0x09, 0x0A, 0x0B, 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27,
    [0x34] = 0x28, 0x29, 0x33, 0x34, 0x35, SPECIAL(MOD, 8), 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42,
    [0x42] = 0x43, 0x44, 0x57, 0x58, PRINT, SPECIAL(MOD, 9), 0x451DE1, SPECIAL(NAV, 0), SPECIAL(NAV, 1),
    [0x4B] = SPECIAL(NAV, 2), SPECIAL(NAV, 3), SPECIAL(NAV, 4), SPECIAL(NAV, 5), SPECIAL(NAV, 6), SPECIAL(NAV, 7),
    [0x51] = SPECIAL(NAV, 8), SPECIAL(NAV, 9), SPECIAL(MOD, 10), 0x35E0, 0x37, 0x4A, 0x4E, 0x1CE0, 0x4F, 0x50,
    [0x5B] = 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53, 0x56, 0x5DE0, 0x5EE0, 0x59, 0x64, 0x65, 0x66,
    [0x6B] = 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x76,
    [0x85] = 0x7E, [0x87] = 0x73, 0x70, 0x7D, 0x79, 0x7B, 0x5C,
    [0x90] = SPECIAL(LANG, 0), SPECIAL(LANG, 1), 0x78, 0x77, 0x76,
    [0xE0] = SPECIAL(MOD, 0), SPECIAL(MOD, 1), SPECIAL(MOD, 2), SPECIAL(MOD, 3), SPECIAL(MOD, 4), SPECIAL(MOD, 5),
    [0xE6] = SPECIAL(MOD, 6), SPECIAL(MOD, 7),
    [0xE9] = 0x5EE0, 0x5FE0, 0x63E0,
};

static const USHORT g_nav[10] = {0x52E0, 0x47E0, 0x49E0, 0x53E0, 0x4FE0, 0x51E0, 0x4DE0, 0x4BE0, 0x50E0, 0x48E0};
static const USHORT g_mods[11] = {0x1D, 0x2A, 0x38, 0x5BE0, 0x1DE0, 0x36, 0x38E0, 0x5CE0, 0x3A, 0x46, 0x45};

static BOOLEAN emit(PHIDP_INSERT_SCANCODES insert, PVOID context, ULONG codes, BOOL broken)
{
    CHAR bytes[4];
    ULONG n = 0;
    for (; n < 4 && (codes >> 8 * n & 0xFF); n++)
        bytes[n] = (CHAR)((codes >> 8 * n & 0xFF) | (broken ? 0x80 : 0));
    return insert(context, bytes, n);
}

static NTSTATUS translate(USAGE usage, BOOL broken, PHIDP_KEYBOARD_MODIFIER_STATE state, PHIDP_INSERT_SCANCODES insert,
                          PVOID context)
{
    ULONG entry = usage < 0x100 ? g_scancodes[usage] : UNKNOWN;
    if (entry == UNKNOWN)
        return HIDP_STATUS_I8042_TRANS_UNKNOWN;
    if (usage == 0x48 && (state->ul & 0x11))
        entry = 0x46E0;
    if ((entry & 0xF0) != 0xF0) {
        emit(insert, context, entry, broken);
        return HIDP_STATUS_SUCCESS;
    }
    ULONG index = entry >> 8;
    switch (entry & 0xFF) {
    case NAV:
        if (index >= RTL_NUMBER_OF(g_nav))
            return HIDP_STATUS_I8042_TRANS_UNKNOWN;
        if (state->ul & 1u << 10 && !broken)
            emit(insert, context, 0x2AE0, FALSE);
        emit(insert, context, g_nav[index], broken);
        if (state->ul & 1u << 10 && broken)
            emit(insert, context, 0x2AE0, TRUE);
        return HIDP_STATUS_SUCCESS;
    case MOD:
        if (index >= RTL_NUMBER_OF(g_mods))
            return HIDP_STATUS_I8042_TRANS_UNKNOWN;
        if (index < 8) {
            if (broken)
                state->ul &= ~(1u << index);
            else
                state->ul |= 1u << index;
        } else if (broken) {
            state->ul &= ~(1u << (index + 16));
        } else if (!(state->ul & 1u << (index + 16))) {
            state->ul ^= 1u << index;
            state->ul |= 1u << (index + 16);
        }
        emit(insert, context, g_mods[index], broken);
        return HIDP_STATUS_SUCCESS;
    case LANG: {
        if (index > 1)
            return HIDP_STATUS_I8042_TRANS_UNKNOWN;
        ULONG code = index ? 0xF1 : 0xF2;
        emit(insert, context, broken ? 0xE1 | code << 8 : code, broken);
        return HIDP_STATUS_SUCCESS;
    }
    case PRINT:
        if (state->ul & 0x44)
            emit(insert, context, 0x54, broken);
        else if (state->ul & 0x33)
            emit(insert, context, 0x37E0, broken);
        else if (!broken) {
            emit(insert, context, 0x2AE0, FALSE);
            emit(insert, context, 0x37E0, FALSE);
        } else {
            emit(insert, context, 0x37E0, TRUE);
            emit(insert, context, 0x2AE0, TRUE);
        }
        return HIDP_STATUS_SUCCESS;
    default:
        return HIDP_STATUS_I8042_TRANS_UNKNOWN;
    }
}

HIDAPI NTSTATUS NTAPI HidP_TranslateUsagesToI8042ScanCodes(PUSAGE list, ULONG length, HIDP_KEYBOARD_DIRECTION action,
                                                           PHIDP_KEYBOARD_MODIFIER_STATE state,
                                                           PHIDP_INSERT_SCANCODES insert, PVOID context)
{
    for (ULONG i = 0; i < length && list[i]; i++) {
        NTSTATUS status = translate(list[i], action == HidP_Keyboard_Break, state, insert, context);
        if (status != HIDP_STATUS_SUCCESS)
            return status;
    }
    return HIDP_STATUS_SUCCESS;
}

/* the cap's unknown global items; on x64 they land over the upper half of the pointer, as windows puts them. */
HIDAPI NTSTATUS NTAPI HidP_GetExtendedAttributes(HIDP_REPORT_TYPE type, USHORT index, PHIDP_PREPARSED_DATA data,
                                                 PHIDP_EXTENDED_ATTRIBUTES attributes, PULONG length)
{
    BLOB b = (BLOB)data;
    if (!signed_ok(b))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    KIND k;
    if (!kind_of(b, type, &k))
        return HIDP_STATUS_INVALID_REPORT_TYPE;
    for (ULONG i = k.First; i < k.Last; i++) {
        CAP c = cap_at(b, i);
        if (index < c.DataMin || index > c.DataMax)
            continue;
        ULONG tokens = c.Flags >> 28, header = sizeof(PVOID) == 8 ? 16 : 12, at = sizeof(PVOID) == 8 ? 12 : 8;
        ULONG room = *length, need = 8 * tokens + at;
        PUCHAR out = (PUCHAR)attributes;
        memset(out, 0, room);
        if (room)
            out[0] = (UCHAR)tokens;
        (void)header;
        if (room > at)
            memcpy(out + at, c.Raw + 28, (room < need ? room : need) - at);
        return room < need ? HIDP_STATUS_BUFFER_TOO_SMALL : HIDP_STATUS_SUCCESS;
    }
    return HIDP_STATUS_DATA_INDEX_NOT_FOUND;
}

HIDAPI NTSTATUS NTAPI HidP_GetVersionInternal(PULONG version)
{
    *version = 2;
    return HIDP_STATUS_SUCCESS;
}
