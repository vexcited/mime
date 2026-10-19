/*
 * strings and numbers of ntdll beyond the core ones in rtl/string.c: sizes and conversions to and from the oem
 * code page, utf-8 strings, counted ansi strings, guid text and the integer conversions of characters.
 *
 * The ansi code page here is windows-1252, which rtl/string.c converts with. The oem one is 437 whose upper half
 * is in <mime/cp437.h>; characters it does not have convert to '?'.
 */
#include "ntdll_private.h"
#include <mime/cp437.h>

#define STRING_MAX 0xFFFF

/* ------------------------------------------------------------------ strings of bytes */

NTDLL_EXPORT NTSTATUS NTAPI RtlInitAnsiStringEx(PANSI_STRING string, PCSTR source)
{
    string->Length = string->MaximumLength = 0;
    string->Buffer = (PSTR)source;
    if (!source)
        return STATUS_SUCCESS;
    SIZE_T length = strlen(source);
    if (length > STRING_MAX - 1)
        return STATUS_NAME_TOO_LONG;
    string->Length = (USHORT)length;
    string->MaximumLength = (USHORT)(length + 1);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT void NTAPI RtlInitUTF8String(PANSI_STRING string, PCSTR source)
{
    RtlInitAnsiString(string, source);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlInitUTF8StringEx(PANSI_STRING string, PCSTR source)
{
    return RtlInitAnsiStringEx(string, source);
}

/* the string is as long as the destination holds. */
NTDLL_EXPORT void NTAPI RtlCopyString(PSTRING destination, const STRING *source)
{
    destination->Length = 0;
    if (!source)
        return;
    USHORT length = source->Length < destination->MaximumLength ? source->Length : destination->MaximumLength;
    destination->Length = length;
    memmove(destination->Buffer, source->Buffer, length);
}

static UCHAR upper_char(UCHAR c)
{
    return c >= 'a' && c <= 'z' ? (UCHAR)(c - 32) : c;
}

NTDLL_EXPORT CHAR NTAPI RtlUpperChar(CHAR c)
{
    return (CHAR)upper_char((UCHAR)c);
}

NTDLL_EXPORT BOOLEAN NTAPI RtlEqualString(const STRING *a, const STRING *b, BOOLEAN case_insensitive)
{
    if (a->Length != b->Length)
        return FALSE;
    for (USHORT i = 0; i < a->Length; i++) {
        UCHAR x = (UCHAR)a->Buffer[i], y = (UCHAR)b->Buffer[i];
        if (x != y && (!case_insensitive || upper_char(x) != upper_char(y)))
            return FALSE;
    }
    return TRUE;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlPrefixString(const STRING *prefix, const STRING *string, BOOLEAN case_insensitive)
{
    if (string->Length < prefix->Length)
        return FALSE;
    for (USHORT i = 0; i < prefix->Length; i++) {
        UCHAR x = (UCHAR)prefix->Buffer[i], y = (UCHAR)string->Buffer[i];
        if (x != y && (!case_insensitive || upper_char(x) != upper_char(y)))
            return FALSE;
    }
    return TRUE;
}

NTDLL_EXPORT void NTAPI RtlFreeOemString(POEM_STRING string)
{
    if (string->Buffer)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, string->Buffer);
}

/* ------------------------------------------------------------------ sizes */

/* what a string needs once converted, in bytes, counting the terminator. */
NTDLL_EXPORT ULONG NTAPI RtlxAnsiStringToUnicodeSize(const STRING *string)
{
    return (ULONG)string->Length * sizeof(WCHAR) + sizeof(WCHAR);
}

NTDLL_EXPORT ULONG NTAPI RtlAnsiStringToUnicodeSize(const STRING *string)
{
    return RtlxAnsiStringToUnicodeSize(string);
}

NTDLL_EXPORT ULONG NTAPI RtlxOemStringToUnicodeSize(const STRING *string)
{
    return RtlxAnsiStringToUnicodeSize(string);
}

NTDLL_EXPORT ULONG NTAPI RtlOemStringToUnicodeSize(const STRING *string)
{
    return RtlxAnsiStringToUnicodeSize(string);
}

NTDLL_EXPORT ULONG NTAPI RtlxUnicodeStringToAnsiSize(PCUNICODE_STRING string)
{
    return string->Length / sizeof(WCHAR) + 1;
}

NTDLL_EXPORT ULONG NTAPI RtlxUnicodeStringToOemSize(PCUNICODE_STRING string)
{
    return string->Length / sizeof(WCHAR) + 1;
}

NTDLL_EXPORT ULONG NTAPI RtlUnicodeStringToOemSize(PCUNICODE_STRING string)
{
    return string->Length / sizeof(WCHAR) + 1;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlMultiByteToUnicodeSize(PULONG size, PCCH source, ULONG source_bytes)
{
    *size = source_bytes * sizeof(WCHAR);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUnicodeToMultiByteSize(PULONG size, PCWCH source, ULONG source_bytes)
{
    *size = source_bytes / sizeof(WCHAR);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ the oem code page */

static WCHAR oem_to_wide(UCHAR c)
{
    return c < 0x80 ? c : g_cp437[c - 0x80];
}

static CHAR wide_to_oem(WCHAR c)
{
    if (c < 0x80)
        return (CHAR)c;
    for (int i = 0; i < 128; i++)
        if (g_cp437[i] == c)
            return (CHAR)(0x80 + i);
    return '?';
}

NTDLL_EXPORT NTSTATUS NTAPI RtlOemToUnicodeN(PWCH dst, ULONG dst_bytes, PULONG written, PCCH src, ULONG src_bytes)
{
    ULONG n = src_bytes < dst_bytes / sizeof(WCHAR) ? src_bytes : dst_bytes / sizeof(WCHAR);
    for (ULONG i = 0; i < n; i++)
        dst[i] = oem_to_wide((UCHAR)src[i]);
    if (written)
        *written = n * sizeof(WCHAR);
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUnicodeToOemN(PCHAR dst, ULONG dst_bytes, PULONG written, PCWCH src, ULONG src_bytes)
{
    ULONG n = src_bytes / sizeof(WCHAR) < dst_bytes ? src_bytes / sizeof(WCHAR) : dst_bytes;
    for (ULONG i = 0; i < n; i++)
        dst[i] = wide_to_oem(src[i]);
    if (written)
        *written = n;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUpcaseUnicodeToOemN(PCHAR dst, ULONG dst_bytes, PULONG written, PCWCH src,
                                                   ULONG src_bytes)
{
    ULONG n = src_bytes / sizeof(WCHAR) < dst_bytes ? src_bytes / sizeof(WCHAR) : dst_bytes;
    for (ULONG i = 0; i < n; i++)
        dst[i] = wide_to_oem(RtlUpcaseUnicodeChar(src[i]));
    if (written)
        *written = n;
    return STATUS_SUCCESS;
}

/* the destination of a conversion: made, or checked to be big enough as the caller's. */
static NTSTATUS prepare_narrow(PSTRING dst, ULONG bytes, BOOLEAN allocate)
{
    if (bytes > STRING_MAX)
        return STATUS_INVALID_PARAMETER_2;
    if (allocate) {
        dst->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, bytes);
        if (!dst->Buffer)
            return STATUS_NO_MEMORY;
        dst->MaximumLength = (USHORT)bytes;
    } else if (bytes > dst->MaximumLength) {
        return STATUS_BUFFER_OVERFLOW;
    }
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlOemStringToUnicodeString(PUNICODE_STRING dst, const STRING *src, BOOLEAN allocate)
{
    ULONG bytes = RtlxOemStringToUnicodeSize(src);
    if (bytes > 0xFFFE)
        return STATUS_INVALID_PARAMETER_2;
    if (allocate) {
        dst->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, bytes);
        if (!dst->Buffer)
            return STATUS_NO_MEMORY;
        dst->MaximumLength = (USHORT)bytes;
    } else if (bytes > dst->MaximumLength) {
        return STATUS_BUFFER_OVERFLOW;
    }
    ULONG written;
    RtlOemToUnicodeN(dst->Buffer, dst->MaximumLength, &written, src->Buffer, src->Length);
    dst->Buffer[written / sizeof(WCHAR)] = 0;
    dst->Length = (USHORT)written;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUnicodeStringToOemString(POEM_STRING dst, PCUNICODE_STRING src, BOOLEAN allocate)
{
    ULONG bytes = RtlxUnicodeStringToOemSize(src);
    NTSTATUS status = prepare_narrow(dst, bytes, allocate);
    if (!NT_SUCCESS(status))
        return status;
    ULONG written;
    RtlUnicodeToOemN(dst->Buffer, dst->MaximumLength, &written, src->Buffer, src->Length);
    dst->Buffer[written] = 0;
    dst->Length = (USHORT)written;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUpcaseUnicodeStringToOemString(POEM_STRING dst, PCUNICODE_STRING src, BOOLEAN allocate)
{
    ULONG bytes = RtlxUnicodeStringToOemSize(src);
    NTSTATUS status = prepare_narrow(dst, bytes, allocate);
    if (!NT_SUCCESS(status))
        return status;
    ULONG written;
    RtlUpcaseUnicodeToOemN(dst->Buffer, dst->MaximumLength, &written, src->Buffer, src->Length);
    dst->Buffer[written] = 0;
    dst->Length = (USHORT)written;
    return STATUS_SUCCESS;
}

/* as the string above, but without a terminator in the buffer, which a counted string does not need. */
NTDLL_EXPORT NTSTATUS NTAPI RtlUpcaseUnicodeStringToCountedOemString(POEM_STRING dst, PCUNICODE_STRING src,
                                                                      BOOLEAN allocate)
{
    ULONG bytes = src->Length / sizeof(WCHAR);
    if (!bytes && !allocate) {
        dst->Length = 0;
        return STATUS_SUCCESS;
    }
    NTSTATUS status = prepare_narrow(dst, bytes, allocate);
    if (!NT_SUCCESS(status))
        return status;
    ULONG written;
    RtlUpcaseUnicodeToOemN(dst->Buffer, dst->MaximumLength, &written, src->Buffer, src->Length);
    dst->Length = (USHORT)written;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUpcaseUnicodeStringToAnsiString(PANSI_STRING dst, PCUNICODE_STRING src,
                                                               BOOLEAN allocate)
{
    ULONG bytes = RtlxUnicodeStringToAnsiSize(src);
    NTSTATUS status = prepare_narrow(dst, bytes, allocate);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING upper = {src->Length, src->Length, nullptr};
    PWSTR buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, src->Length + sizeof(WCHAR));
    if (!buffer) {
        if (allocate)
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, dst->Buffer);
        return STATUS_NO_MEMORY;
    }
    upper.Buffer = buffer;
    RtlUpcaseUnicodeString(&upper, src, FALSE);
    ULONG written;
    RtlUnicodeToMultiByteN(dst->Buffer, dst->MaximumLength, &written, buffer, src->Length);
    dst->Buffer[written] = 0;
    dst->Length = (USHORT)written;
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, buffer);
    return STATUS_SUCCESS;
}

/* the characters a FAT name may not have: control characters and those that make a path or a pattern. */
static BOOLEAN fat_illegal(UCHAR c)
{
    return c < 0x20 || strchr("\"*+,/:;<=>?[\\]|", c) != nullptr;
}

/*
 * whether a name is one of the 8.3 names of a FAT volume: at most eight characters, a dot and three more, no
 * characters the format forbids, no trailing dot or blank, no blank before the dot. Spaces are legal and
 * reported. "." and ".." are legal too.
 */
NTDLL_EXPORT BOOLEAN NTAPI RtlIsNameLegalDOS8Dot3(PCUNICODE_STRING name, POEM_STRING oem, PBOOLEAN contains_spaces)
{
    if (name->Length > 0x18)
        return FALSE;
    /* twelve bytes are all a name can use. */
    CHAR local[12];
    OEM_STRING counted = {0, sizeof(local), local};
    POEM_STRING converted = oem ? oem : &counted;
    if (!NT_SUCCESS(RtlUpcaseUnicodeStringToCountedOemString(converted, name, FALSE)))
        return FALSE;
    const UCHAR *text = (const UCHAR *)converted->Buffer;
    ULONG length = converted->Length;
    if ((length == 1 && text[0] == '.') || (length == 2 && text[0] == '.' && text[1] == '.')) {
        if (contains_spaces)
            *contains_spaces = FALSE;
        return TRUE;
    }
    BOOLEAN dot = FALSE, spaces = FALSE;
    UCHAR last = 0;
    for (ULONG i = 0; i < length; i++) {
        last = text[i];
        if (last < 0x80 && fat_illegal(last))
            return FALSE;
        if (last == ' ') {
            spaces = TRUE;
        } else if (last == '.') {
            if (dot || i == 0 || text[i - 1] == ' ' || length - i - 1 > 3)
                return FALSE;
            dot = TRUE;
        }
        if (i >= 8 && !dot)
            return FALSE;
    }
    if (last == ' ' || last == '.')
        return FALSE;
    if (contains_spaces)
        *contains_spaces = spaces;
    return TRUE;
}

NTDLL_EXPORT ULONG NTAPI RtlGetLongestNtPathLength(void)
{
    return 269;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlDowncaseUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src, BOOLEAN allocate)
{
    if (!allocate && !src->Length) {
        dst->Length = 0;
        return STATUS_SUCCESS;
    }
    if (allocate) {
        dst->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, src->Length);
        if (!dst->Buffer) {
            dst->MaximumLength = 0;
            return STATUS_NO_MEMORY;
        }
        dst->MaximumLength = src->Length;
    } else if (src->Length > dst->MaximumLength || !dst->Buffer) {
        return STATUS_BUFFER_OVERFLOW;
    }
    for (USHORT i = 0; i < src->Length / sizeof(WCHAR); i++)
        dst->Buffer[i] = RtlDowncaseUnicodeChar(src->Buffer[i]);
    dst->Length = src->Length;
    return STATUS_SUCCESS;
}

/*
 * a copy of a string: with flag 1 the copy has a terminator, with 2 as well it exists when the string is empty.
 * Flag 2 asks for flag 1 too. A string has to be well formed and shorter than 0xFFFE bytes when a terminator
 * is added.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlDuplicateUnicodeString(ULONG flags, PCUNICODE_STRING source, PUNICODE_STRING copy)
{
    if ((flags & ~3u) || !copy || ((flags & 2) && !(flags & 1)))
        return STATUS_INVALID_PARAMETER;
    if (source && ((source->Length & 1) || (source->MaximumLength & 1) || source->Length > source->MaximumLength ||
                   source->MaximumLength == 0xFFFF || (!source->Buffer && (source->Length || source->MaximumLength))))
        return STATUS_INVALID_PARAMETER;
    USHORT length = source ? source->Length : 0;
    if ((flags & 1) && length == 0xFFFE)
        return STATUS_NAME_TOO_LONG;
    USHORT size = (flags & 1) ? (USHORT)(length + 2) : length;
    if (!(flags & 2) && !length)
        size = 0;
    PWSTR buffer = nullptr;
    if (size) {
        buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
        if (!buffer)
            return STATUS_NO_MEMORY;
        if (length)
            memmove(buffer, source->Buffer, length);
        if (flags & 1)
            buffer[length / sizeof(WCHAR)] = 0;
    }
    copy->Length = length;
    copy->MaximumLength = size;
    copy->Buffer = buffer;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlCreateUnicodeStringFromAsciiz(PUNICODE_STRING dst, PCSTR source)
{
    ANSI_STRING ansi = {};
    if (source) {
        SIZE_T length = strlen(source);
        if (length > STRING_MAX - 1)
            return FALSE;
        ansi.Length = (USHORT)length;
        ansi.MaximumLength = (USHORT)(length + 1);
    }
    ansi.Buffer = (PSTR)source;
    return NT_SUCCESS(RtlAnsiStringToUnicodeString(dst, &ansi, TRUE));
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUTF8StringToUnicodeString(PUNICODE_STRING dst, const STRING *src, BOOLEAN allocate)
{
    ULONG bytes = 0;
    NTSTATUS status = RtlUTF8ToUnicodeN(nullptr, 0, &bytes, src->Buffer, src->Length);
    if (!NT_SUCCESS(status))
        return status;
    bytes += sizeof(WCHAR);
    if (bytes > 0xFFFE)
        return STATUS_INVALID_PARAMETER_2;
    if (allocate) {
        dst->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, bytes);
        if (!dst->Buffer)
            return STATUS_NO_MEMORY;
        dst->MaximumLength = (USHORT)bytes;
    } else if (bytes > dst->MaximumLength) {
        return STATUS_BUFFER_OVERFLOW;
    }
    status = RtlUTF8ToUnicodeN(dst->Buffer, dst->MaximumLength, &bytes, src->Buffer, src->Length);
    if (!NT_SUCCESS(status)) {
        if (allocate) {
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, dst->Buffer);
            dst->Buffer = nullptr;
            dst->MaximumLength = 0;
        }
        return status;
    }
    dst->Length = (USHORT)bytes;
    dst->Buffer[bytes / sizeof(WCHAR)] = 0;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlUnicodeStringToUTF8String(PSTRING dst, PCUNICODE_STRING src, BOOLEAN allocate)
{
    ULONG bytes = 0;
    NTSTATUS status = RtlUnicodeToUTF8N(nullptr, 0, &bytes, src->Buffer, src->Length);
    if (!NT_SUCCESS(status))
        return status;
    bytes += 1;
    status = prepare_narrow(dst, bytes, allocate);
    if (!NT_SUCCESS(status))
        return status;
    status = RtlUnicodeToUTF8N(dst->Buffer, dst->MaximumLength, &bytes, src->Buffer, src->Length);
    if (!NT_SUCCESS(status)) {
        if (allocate) {
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, dst->Buffer);
            dst->Buffer = nullptr;
            dst->MaximumLength = 0;
        }
        return status;
    }
    dst->Buffer[bytes] = 0;
    dst->Length = (USHORT)bytes;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ characters and integers */

/*
 * the character at the string as a wide one, and the string moved past it. Windows-1252 has one byte per
 * character.
 */
NTDLL_EXPORT WCHAR NTAPI RtlAnsiCharToUnicodeChar(PUCHAR *source)
{
    WCHAR c;
    ULONG written;
    RtlMultiByteToUnicodeN(&c, sizeof(c), &written, (PCCH)*source, 1);
    (*source)++;
    return c;
}

static int digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/*
 * the number at the start of a string. Leading blanks and control characters are skipped, then a sign. Base 0
 * reads 0x, 0o and 0b prefixes and is decimal otherwise; 2, 8, 10 and 16 are the others. It stops at the first
 * character that is not a digit of the base, and overflow wraps.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlCharToInteger(PCSTR string, ULONG base, PULONG value)
{
    const char *p = string;
    while (*p && *p <= ' ')
        p++;
    BOOLEAN negative = *p == '-';
    if (*p == '-' || *p == '+')
        p++;
    if (base == 0) {
        base = 10;
        if (p[0] == '0') {
            if (p[1] == 'x') {
                base = 16;
                p += 2;
            } else if (p[1] == 'o') {
                base = 8;
                p += 2;
            } else if (p[1] == 'b') {
                base = 2;
                p += 2;
            } else {
                p++;
            }
        }
    } else if (base != 2 && base != 8 && base != 10 && base != 16) {
        return STATUS_INVALID_PARAMETER;
    }
    ULONG result = 0;
    for (;; p++) {
        int digit = digit_value(*p);
        if (digit < 0 || (ULONG)digit >= base)
            break;
        result = result * base + (ULONG)digit;
    }
    *value = negative ? -result : result;
    return STATUS_SUCCESS;
}

/*
 * the number as digits in a buffer of the given length. A negative length is a width: the number is padded
 * with zeros to it. Digits that do not fit give STATUS_BUFFER_OVERFLOW and the buffer is left alone; a number
 * shorter than the buffer is ended by a null.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlIntegerToChar(ULONG value, ULONG base, LONG length, PSTR buffer)
{
    if (base == 0)
        base = 10;
    if (base != 2 && base != 8 && base != 10 && base != 16)
        return STATUS_INVALID_PARAMETER;
    char digits[33];
    int count = 0;
    do {
        digits[count++] = "0123456789ABCDEF"[value % base];
        value /= base;
    } while (value);
    LONG capacity = length;
    LONG padding = 0;
    if (length < 0) {
        capacity = -length;
        if (count < capacity)
            padding = capacity - count;
    }
    if (count + padding > capacity)
        return STATUS_BUFFER_OVERFLOW;
    memset(buffer, '0', padding);
    for (int i = 0; i < count; i++)
        buffer[padding + i] = digits[count - 1 - i];
    if (count + padding < capacity)
        buffer[count + padding] = 0;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ guids */

/* {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}: exactly this, thirty eight characters, with hexadecimal digits of any case. */
NTDLL_EXPORT NTSTATUS NTAPI RtlGUIDFromString(PCUNICODE_STRING string, GUID *guid)
{
    static const UCHAR layout[] = {8, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2};
    const WCHAR *p = string->Buffer;
    ULONG remaining = string->Length / sizeof(WCHAR);
    ULONG parts[11];
    if (remaining < 1 || *p != L'{')
        return STATUS_INVALID_PARAMETER;
    p++;
    remaining--;
    for (int i = 0; i < 11; i++) {
        ULONG value = 0;
        for (int d = 0; d < layout[i]; d++) {
            if (!remaining || *p > 'f')
                return STATUS_INVALID_PARAMETER;
            int digit = digit_value((char)*p);
            if (digit < 0)
                return STATUS_INVALID_PARAMETER;
            value = value * 16 + (ULONG)digit;
            p++;
            remaining--;
        }
        parts[i] = value;
        /* hyphens follow the second, third, fourth and fifth groups. */
        if (i == 0 || i == 1 || i == 2 || i == 4) {
            if (!remaining || *p != L'-')
                return STATUS_INVALID_PARAMETER;
            p++;
            remaining--;
        }
    }
    if (!remaining || *p != L'}' || remaining != 1)
        return STATUS_INVALID_PARAMETER;
    guid->Data1 = parts[0];
    guid->Data2 = (USHORT)parts[1];
    guid->Data3 = (USHORT)parts[2];
    for (int i = 0; i < 8; i++)
        guid->Data4[i] = (UCHAR)parts[3 + i];
    return STATUS_SUCCESS;
}

/*
 * the text of a guid in braces, in lower case hexadecimal, 76 bytes long. Allocated, the string gets 78 bytes;
 * otherwise it has to have them.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlStringFromGUIDEx(const GUID *guid, PUNICODE_STRING string, BOOLEAN allocate)
{
    if (allocate) {
        string->MaximumLength = 78;
        string->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, 78);
        if (!string->Buffer)
            return STATUS_NO_MEMORY;
    } else if (string->MaximumLength < 78) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    static const WCHAR hex[] = L"0123456789abcdef";
    PWSTR out = string->Buffer;
    *out++ = L'{';
    for (int shift = 28; shift >= 0; shift -= 4)
        *out++ = hex[(guid->Data1 >> shift) & 15];
    *out++ = L'-';
    for (int shift = 12; shift >= 0; shift -= 4)
        *out++ = hex[(guid->Data2 >> shift) & 15];
    *out++ = L'-';
    for (int shift = 12; shift >= 0; shift -= 4)
        *out++ = hex[(guid->Data3 >> shift) & 15];
    *out++ = L'-';
    for (int i = 0; i < 8; i++) {
        *out++ = hex[guid->Data4[i] >> 4];
        *out++ = hex[guid->Data4[i] & 15];
        if (i == 1)
            *out++ = L'-';
    }
    *out++ = L'}';
    *out = 0;
    string->Length = 76;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlStringFromGUID(const GUID *guid, PUNICODE_STRING string)
{
    return RtlStringFromGUIDEx(guid, string, TRUE);
}
