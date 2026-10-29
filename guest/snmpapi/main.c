/*
 * snmpapi.dll: the utility functions of SNMP, which are memory, copies and comparisons of the object identifiers, octet
 * strings and values that extension agents and managers trade, and conversions of text. the service and transport
 * calls of the agent side are not here.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define SNMPAPI_NOERROR 1
#define SNMPAPI_ERROR 0

#define ASN_BITS 0x03
#define ASN_OCTETSTRING 0x04
#define ASN_OBJECTIDENTIFIER 0x06
#define ASN_SEQUENCE 0x30
#define ASN_IPADDRESS 0x40
#define ASN_OPAQUE 0x44

typedef struct {
    UINT idLength;
    UINT *ids;
} AsnObjectIdentifier;

typedef struct {
    BYTE *stream;
    UINT length;
    BOOL dynamic;
} AsnOctetString;

typedef struct {
    BYTE asnType;
    union {
        LONG number;
        ULONG unsigned32;
        ULONGLONG counter64;
        AsnOctetString string;
        AsnOctetString bits;
        AsnObjectIdentifier object;
        AsnOctetString sequence;
        AsnOctetString address;
        ULONG counter;
        ULONG gauge;
        ULONG ticks;
        AsnOctetString arbitrary;
    } asnValue;
} AsnAny;

typedef struct {
    AsnObjectIdentifier name;
    AsnAny value;
} SnmpVarBind;

typedef struct {
    SnmpVarBind *list;
    UINT len;
} SnmpVarBindList;

LPVOID WINAPI SnmpUtilMemAlloc(UINT size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void WINAPI SnmpUtilMemFree(LPVOID memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

LPVOID WINAPI SnmpUtilMemReAlloc(LPVOID memory, UINT size)
{
    if (!memory)
        return SnmpUtilMemAlloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, memory, size ? size : 1);
}

/* ------------------------------------------------------------------ object identifiers */

void WINAPI SnmpUtilOidFree(AsnObjectIdentifier *oid)
{
    if (!oid)
        return;
    SnmpUtilMemFree(oid->ids);
    oid->ids = nullptr;
    oid->idLength = 0;
}

INT WINAPI SnmpUtilOidCpy(AsnObjectIdentifier *destination, AsnObjectIdentifier *source)
{
    destination->ids = SnmpUtilMemAlloc(source->idLength * sizeof(UINT));
    if (!destination->ids) {
        destination->idLength = 0;
        return SNMPAPI_ERROR;
    }
    destination->idLength = source->idLength;
    memcpy(destination->ids, source->ids, source->idLength * sizeof(UINT));
    return SNMPAPI_NOERROR;
}

INT WINAPI SnmpUtilOidAppend(AsnObjectIdentifier *destination, AsnObjectIdentifier *source)
{
    if (!source->idLength)
        return SNMPAPI_NOERROR;
    UINT *ids = SnmpUtilMemReAlloc(destination->ids, (destination->idLength + source->idLength) * sizeof(UINT));
    if (!ids)
        return SNMPAPI_ERROR;
    memcpy(ids + destination->idLength, source->ids, source->idLength * sizeof(UINT));
    destination->ids = ids;
    destination->idLength += source->idLength;
    return SNMPAPI_NOERROR;
}

/* the first n sub identifiers are compared, and the shorter identifier is the smaller when they agree. */
INT WINAPI SnmpUtilOidNCmp(AsnObjectIdentifier *a, AsnObjectIdentifier *b, UINT n)
{
    UINT count = a->idLength < b->idLength ? a->idLength : b->idLength;
    if (count > n)
        count = n;
    for (UINT i = 0; i < count; i++)
        if (a->ids[i] != b->ids[i])
            return a->ids[i] < b->ids[i] ? -1 : 1;
    if (count == n)
        return 0;
    return a->idLength == b->idLength ? 0 : a->idLength < b->idLength ? -1 : 1;
}

INT WINAPI SnmpUtilOidCmp(AsnObjectIdentifier *a, AsnObjectIdentifier *b)
{
    return SnmpUtilOidNCmp(a, b, 0xFFFFFFFF);
}

/* the identifier in dotted form, in a buffer that the next call reuses. */
LPSTR WINAPI SnmpUtilIdsToA(UINT *ids, UINT count)
{
    static char text[1024];
    int at = 0;
    text[0] = 0;
    for (UINT i = 0; i < count && at < (int)sizeof(text) - 12; i++) {
        char digits[11];
        int n = 0;
        UINT value = ids[i];
        do {
            digits[n++] = (char)('0' + value % 10);
            value /= 10;
        } while (value);
        if (i)
            text[at++] = '.';
        while (n)
            text[at++] = digits[--n];
        text[at] = 0;
    }
    return text;
}

LPSTR WINAPI SnmpUtilOidToA(AsnObjectIdentifier *oid)
{
    return SnmpUtilIdsToA(oid->ids, oid->idLength);
}

/* ------------------------------------------------------------------ octet strings */

void WINAPI SnmpUtilOctetsFree(AsnOctetString *octets)
{
    if (!octets)
        return;
    if (octets->dynamic && octets->stream)
        SnmpUtilMemFree(octets->stream);
    octets->stream = nullptr;
    octets->length = 0;
    octets->dynamic = FALSE;
}

INT WINAPI SnmpUtilOctetsCpy(AsnOctetString *destination, AsnOctetString *source)
{
    destination->stream = nullptr;
    destination->length = 0;
    destination->dynamic = FALSE;
    if (!source->length)
        return SNMPAPI_NOERROR;
    destination->stream = SnmpUtilMemAlloc(source->length);
    if (!destination->stream)
        return SNMPAPI_ERROR;
    memcpy(destination->stream, source->stream, source->length);
    destination->length = source->length;
    destination->dynamic = TRUE;
    return SNMPAPI_NOERROR;
}

INT WINAPI SnmpUtilOctetsNCmp(AsnOctetString *a, AsnOctetString *b, UINT n)
{
    UINT count = a->length < b->length ? a->length : b->length;
    if (count > n)
        count = n;
    for (UINT i = 0; i < count; i++)
        if (a->stream[i] != b->stream[i])
            return a->stream[i] < b->stream[i] ? -1 : 1;
    if (count == n)
        return 0;
    return a->length == b->length ? 0 : a->length < b->length ? -1 : 1;
}

INT WINAPI SnmpUtilOctetsCmp(AsnOctetString *a, AsnOctetString *b)
{
    return SnmpUtilOctetsNCmp(a, b, 0xFFFFFFFF);
}

/* ------------------------------------------------------------------ values and variable bindings */

static BOOL is_octets(BYTE type)
{
    return type == ASN_OCTETSTRING || type == ASN_BITS || type == ASN_SEQUENCE || type == ASN_IPADDRESS ||
           type == ASN_OPAQUE;
}

void WINAPI SnmpUtilAsnAnyFree(AsnAny *any)
{
    if (!any)
        return;
    if (is_octets(any->asnType))
        SnmpUtilOctetsFree(&any->asnValue.string);
    else if (any->asnType == ASN_OBJECTIDENTIFIER)
        SnmpUtilOidFree(&any->asnValue.object);
    any->asnType = 0;
}

INT WINAPI SnmpUtilAsnAnyCpy(AsnAny *destination, AsnAny *source)
{
    *destination = *source;
    if (is_octets(source->asnType))
        return SnmpUtilOctetsCpy(&destination->asnValue.string, &source->asnValue.string);
    if (source->asnType == ASN_OBJECTIDENTIFIER)
        return SnmpUtilOidCpy(&destination->asnValue.object, &source->asnValue.object);
    return SNMPAPI_NOERROR;
}

void WINAPI SnmpUtilVarBindFree(SnmpVarBind *binding)
{
    if (!binding)
        return;
    SnmpUtilAsnAnyFree(&binding->value);
    SnmpUtilOidFree(&binding->name);
}

INT WINAPI SnmpUtilVarBindCpy(SnmpVarBind *destination, SnmpVarBind *source)
{
    if (!SnmpUtilOidCpy(&destination->name, &source->name))
        return SNMPAPI_ERROR;
    if (!SnmpUtilAsnAnyCpy(&destination->value, &source->value)) {
        SnmpUtilOidFree(&destination->name);
        return SNMPAPI_ERROR;
    }
    return SNMPAPI_NOERROR;
}

void WINAPI SnmpUtilVarBindListFree(SnmpVarBindList *list)
{
    if (!list)
        return;
    for (UINT i = 0; i < list->len; i++)
        SnmpUtilVarBindFree(&list->list[i]);
    SnmpUtilMemFree(list->list);
    list->list = nullptr;
    list->len = 0;
}

INT WINAPI SnmpUtilVarBindListCpy(SnmpVarBindList *destination, SnmpVarBindList *source)
{
    destination->len = 0;
    destination->list = SnmpUtilMemAlloc(source->len * sizeof(SnmpVarBind));
    if (!destination->list)
        return SNMPAPI_ERROR;
    for (UINT i = 0; i < source->len; i++) {
        if (!SnmpUtilVarBindCpy(&destination->list[i], &source->list[i])) {
            SnmpUtilVarBindListFree(destination);
            return SNMPAPI_ERROR;
        }
        destination->len = i + 1;
    }
    return SNMPAPI_NOERROR;
}

/* ------------------------------------------------------------------ text */

INT WINAPI SnmpUtilUnicodeToAnsi(LPSTR *ansi, LPWSTR unicode, BOOL allocate)
{
    int bytes = WideCharToMultiByte(CP_ACP, 0, unicode, -1, nullptr, 0, nullptr, nullptr);
    if (!bytes)
        return SNMPAPI_ERROR;
    if (allocate) {
        *ansi = SnmpUtilMemAlloc(bytes);
        if (!*ansi)
            return SNMPAPI_ERROR;
    }
    WideCharToMultiByte(CP_ACP, 0, unicode, -1, *ansi, bytes, nullptr, nullptr);
    return SNMPAPI_NOERROR;
}

INT WINAPI SnmpUtilAnsiToUnicode(LPWSTR *unicode, LPSTR ansi, BOOL allocate)
{
    int chars = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (!chars)
        return SNMPAPI_ERROR;
    if (allocate) {
        *unicode = SnmpUtilMemAlloc(chars * sizeof(WCHAR));
        if (!*unicode)
            return SNMPAPI_ERROR;
    }
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, *unicode, chars);
    return SNMPAPI_NOERROR;
}

INT WINAPI SnmpUtilUnicodeToUTF8(LPWSTR *utf8, LPWSTR unicode, BOOL allocate)
{
    int bytes = WideCharToMultiByte(CP_UTF8, 0, unicode, -1, nullptr, 0, nullptr, nullptr);
    if (!bytes)
        return SNMPAPI_ERROR;
    if (allocate) {
        *utf8 = SnmpUtilMemAlloc(bytes);
        if (!*utf8)
            return SNMPAPI_ERROR;
    }
    WideCharToMultiByte(CP_UTF8, 0, unicode, -1, (LPSTR)*utf8, bytes, nullptr, nullptr);
    return SNMPAPI_NOERROR;
}

INT WINAPI SnmpUtilUTF8ToUnicode(LPWSTR *unicode, LPSTR utf8, BOOL allocate)
{
    int chars = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (!chars)
        return SNMPAPI_ERROR;
    if (allocate) {
        *unicode = SnmpUtilMemAlloc(chars * sizeof(WCHAR));
        if (!*unicode)
            return SNMPAPI_ERROR;
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, *unicode, chars);
    return SNMPAPI_NOERROR;
}
