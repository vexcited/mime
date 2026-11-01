/*
 * the text helpers windows keeps in sechost.dll for its security descriptor language: the digit and operator tables of
 * conditional aces, operand values, attribute names and the Local functions that turn conditions, sids and resource
 * attributes into text and back. the parsing and printing is that of sddl.c.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"

#define ERROR_INVALID_CONDITION_MAGIC 805
#define CT_INT64 0x04
#define CT_COMPOSITE 0x50
#define CT_SID 0x51

static const WCHAR g_digits[] = L"0123456789ABCDEF";

ADVAPI WCHAR WINAPI GetCharFromDigit(UCHAR digit, PWCHAR out)
{
    WCHAR c = digit < 16 ? g_digits[digit] : 0;
    *out = c;
    return c;
}

/* a hex digit, or the number sign which counts as zero. */
ADVAPI BOOLEAN WINAPI GetDigitFromChar(WCHAR c, PUCHAR out)
{
    if (c >= L'0' && c <= L'9')
        *out = (UCHAR)(c - L'0');
    else if (c >= L'A' && c <= L'F')
        *out = (UCHAR)(c - L'A' + 10);
    else if (c >= L'a' && c <= L'f')
        *out = (UCHAR)(c - L'a' + 10);
    else if (c == L'#')
        *out = 0;
    else
        return FALSE;
    return TRUE;
}

ADVAPI UCHAR WINAPI GetOperatorCodeAtIndex(DWORD index)
{
    BYTE token;
    BOOL unary;
    return AdvOperatorAt(index, &token, &unary) ? token : 0;
}

ADVAPI UCHAR WINAPI GetOperatorUnaryAtIndex(DWORD index)
{
    BYTE token;
    BOOL unary;
    return AdvOperatorAt(index, &token, &unary) ? unary : 0;
}

ADVAPI DWORD WINAPI GetOperatorIndexByToken(UCHAR token)
{
    BYTE found;
    BOOL unary;
    for (DWORD i = 0; AdvOperatorAt(i, &found, &unary); i++)
        if (found == token)
            return i;
    return 0xFFFFFFFF;
}

/* the operand types holding several values: the membership and containment operators' tokens. */
ADVAPI UCHAR WINAPI IsArrayType(UCHAR type)
{
    return type == 0x86 || (type >= 0x88 && type <= 0x8C) || (type >= 0x8E && type <= 0x93);
}

ADVAPI UCHAR WINAPI IsValueSizeFixed(UCHAR type)
{
    return type >= 1 && type <= 4;
}

/* ------------------------------------------------------------------ attribute names */

/* the characters of a name written as a percent sign and four hex digits. */
ADVAPI DWORD WINAPI EncodeAttributeName(PCWSTR name, DWORD size, PWSTR *result)
{
    if (!name || !size || !result)
        return ERROR_INVALID_PARAMETER;
    if (size & 1)
        return ERROR_INVALID_ACL;
    PWSTR out = LocalAlloc(LMEM_ZEROINIT, 5 * (SIZE_T)size + 2);
    *result = out;
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD at = 0;
    for (DWORD i = 0; i < size / 2; i++) {
        WCHAR c = name[i];
        if (AdvNameCharNeedsEncoding(c)) {
            out[at++] = L'%';
            out[at++] = g_digits[c >> 12];
            out[at++] = g_digits[(c >> 8) & 15];
            out[at++] = g_digits[(c >> 4) & 15];
            out[at++] = g_digits[c & 15];
        } else {
            out[at++] = c;
        }
    }
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI DecodeAttributeName(PCWSTR name, DWORD size, PWSTR *result)
{
    if (!name || !size || !result)
        return ERROR_INVALID_PARAMETER;
    if (size & 1)
        return ERROR_INVALID_ACL;
    PWSTR out = LocalAlloc(LMEM_ZEROINIT, (SIZE_T)size + 2);
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD chars = size / 2, at = 0;
    for (DWORD i = 0; i < chars; i++) {
        WCHAR c = name[i];
        if (c == L'%') {
            UCHAR digit[4];
            if (i + 4 >= chars || !GetDigitFromChar(name[i + 1], &digit[0]) ||
                !GetDigitFromChar(name[i + 2], &digit[1]) || !GetDigitFromChar(name[i + 3], &digit[2]) ||
                !GetDigitFromChar(name[i + 4], &digit[3])) {
                LocalFree(out);
                return ERROR_INVALID_ACL;
            }
            c = (WCHAR)(digit[0] << 12 | digit[1] << 8 | digit[2] << 4 | digit[3]);
            if (!AdvNameCharNeedsEncoding(c)) {
                LocalFree(out);
                return ERROR_INVALID_ACL;
            }
            i += 4;
        }
        out[at++] = c;
    }
    *result = out;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ operand values */

/* the value of one operand as the condition parser holds it: Static says the value belongs to a table. */
typedef struct OPERAND_VALUE {
    BOOLEAN Static;
    UCHAR Type;
    DWORD Size;
    PVOID Value;
} OPERAND_VALUE;

ADVAPI void WINAPI FreeOperandValue(OPERAND_VALUE *operand)
{
    if (!operand)
        return;
    if (operand->Value && !operand->Static)
        LocalFree(operand->Value);
    LocalFree(operand);
}

/* the type of each element of a composite value. */
static BOOL composite_types_equal(const BYTE *p, DWORD size)
{
    UCHAR first = 0;
    for (DWORD at = 0; at < size;) {
        UCHAR type = p[at];
        if (first && type != first)
            return FALSE;
        first = type;
        DWORD length;
        if (type == CT_INT64) {
            at += 11;
            continue;
        }
        memcpy(&length, p + at + 1, sizeof(length));
        at += 5 + length;
    }
    return TRUE;
}

/*
 * parses the operand at the start of the text. attribute reads a name, same_type demands the elements of a
 * composite to share a type. consumed gets the characters taken.
 */
ADVAPI DWORD WINAPI GetOperandValue(PCWSTR text, BOOLEAN attribute, BOOLEAN same_type, OPERAND_VALUE **result,
                                    DWORD *consumed, PVOID a6, PVOID a7, PVOID a8, BOOLEAN a9)
{
    (void)a6;
    (void)a7;
    (void)a8;
    (void)a9;
    BYTE *token;
    DWORD length, taken;
    *consumed = 0;
    DWORD error = AdvOperandFromString(text, attribute, nullptr, &token, &length, &taken);
    if (error) {
        *result = nullptr;
        return error;
    }
    OPERAND_VALUE *operand = LocalAlloc(LMEM_ZEROINIT, sizeof(*operand));
    if (!operand) {
        AdvFree(token);
        *result = nullptr;
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    operand->Type = token[0];
    const BYTE *data;
    if (IsValueSizeFixed(token[0])) {
        operand->Size = 10;
        data = token + 1;
    } else {
        memcpy(&operand->Size, token + 1, sizeof(DWORD));
        data = token + 5;
    }
    if (same_type && token[0] == CT_COMPOSITE && !composite_types_equal(data, operand->Size))
        error = ERROR_INVALID_ACL;
    else if (operand->Size) {
        operand->Value = LocalAlloc(LMEM_FIXED, operand->Size);
        if (operand->Value)
            memcpy(operand->Value, data, operand->Size);
        else
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    AdvFree(token);
    if (error) {
        FreeOperandValue(operand);
        *result = nullptr;
        return error;
    }
    *consumed = taken;
    *result = operand;
    return ERROR_SUCCESS;
}

/* prints the operand at the start of the bytes; used gets how many bytes it took. */
ADVAPI DWORD WINAPI GetPrintableOperandValue(const BYTE *token, DWORD size, PWSTR *text, DWORD *used, PVOID a5, PVOID a6,
                                             PVOID a7, BOOLEAN a8)
{
    (void)a5;
    (void)a6;
    (void)a7;
    (void)a8;
    if (!token || !text || !size)
        return ERROR_INVALID_PARAMETER;
    PWSTR printed;
    DWORD error = AdvOperandToString(token, size, nullptr, &printed, used);
    if (error)
        return error;
    SIZE_T bytes = (wcslen(printed) + 1) * sizeof(WCHAR);
    *text = LocalAlloc(LMEM_FIXED, bytes);
    if (*text)
        memcpy(*text, printed, bytes);
    else
        error = ERROR_NOT_ENOUGH_MEMORY;
    AdvFree(printed);
    return error;
}

/* ------------------------------------------------------------------ conditions, sids and resource attributes */

/* compiles the condition at the text. end gets where it stopped, and with strict nothing may follow it. */
ADVAPI DWORD WINAPI LocalGetConditionForString(PCWSTR text, PCWSTR *end, PVOID *binary, DWORD *size, PVOID a5,
                                               PVOID a6, PVOID a7, BOOLEAN a8, BOOLEAN strict)
{
    (void)a5;
    (void)a6;
    (void)a7;
    (void)a8;
    if (!text || !end || !binary || !size)
        return ERROR_INVALID_PARAMETER;
    *binary = nullptr;
    *size = 0;
    PCWSTR stop;
    DWORD error = AdvConditionCompile(text, nullptr, binary, size, &stop);
    if (!error && strict && *stop) {
        LocalFree(*binary);
        *binary = nullptr;
        *size = 0;
        error = ERROR_INVALID_ACL;
    }
    if (!error)
        *end = stop;
    return error;
}

static DWORD condition_string(const BYTE *binary, DWORD size, PWSTR *result, DWORD *types)
{
    if (!binary || !result || !size)
        return ERROR_INVALID_PARAMETER;
    if (size < 6 || memcmp(binary, "artx", 4))
        return ERROR_INVALID_CONDITION_MAGIC;
    PWSTR text;
    DWORD error = AdvConditionToString(binary, size, nullptr, &text, types);
    if (error)
        return error;
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    *result = LocalAlloc(LMEM_FIXED, bytes);
    if (*result)
        memcpy(*result, text, bytes);
    else
        error = ERROR_NOT_ENOUGH_MEMORY;
    AdvFree(text);
    return error;
}

ADVAPI DWORD WINAPI LocalGetStringForCondition(const BYTE *binary, DWORD size, PWSTR *result, PVOID a4, PVOID a5,
                                               PVOID a6, BOOLEAN a7)
{
    (void)a4;
    (void)a5;
    (void)a6;
    (void)a7;
    return condition_string(binary, size, result, nullptr);
}

/* the kinds of claims a condition reads: 1 user, 2 resource, 4 device, 8 token. */
ADVAPI DWORD WINAPI LocalGetReferencedTokenTypesForCondition(const BYTE *binary, DWORD size, DWORD *types)
{
    PWSTR text = nullptr;
    DWORD error = condition_string(binary, size, &text, types);
    if (text)
        LocalFree(text);
    return error;
}

ADVAPI DWORD WINAPI LocalGetSidForString(PCWSTR text, PSID *sid, PCWSTR *end, BOOLEAN *allocated, PVOID a5, PVOID a6,
                                         PVOID a7, BOOLEAN a8)
{
    (void)a5;
    (void)a6;
    (void)a7;
    (void)a8;
    if (!text || !sid || !end || !allocated)
        return ERROR_INVALID_PARAMETER;
    *allocated = FALSE;
    if (!text[0] || !text[1])
        return 1332;
    BYTE storage[SECURITY_MAX_SID_SIZE];
    DWORD error = AdvParseSidString(text, nullptr, storage, end);
    if (error)
        return error;
    DWORD length = AdvSidLength((const SID *)storage);
    *sid = LocalAlloc(LMEM_FIXED, length);
    if (!*sid)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(*sid, storage, length);
    *allocated = TRUE;
    return ERROR_SUCCESS;
}

/* a resource attribute in its relative form as the text of its ace. */
ADVAPI DWORD WINAPI LocalGetStringForRelativeAttribute(const BYTE *attribute, DWORD size, PVOID a3, PVOID a4, PVOID a5,
                                                       BOOLEAN a6, PWSTR *result)
{
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    if (!attribute || !size || !result)
        return ERROR_INVALID_PARAMETER;
    PWSTR text;
    DWORD error = AdvResourceAttributeToString(attribute, size, nullptr, &text);
    if (error)
        return error;
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    *result = LocalAlloc(LMEM_FIXED, bytes);
    if (*result)
        memcpy(*result, text, bytes);
    else
        error = ERROR_NOT_ENOUGH_MEMORY;
    AdvFree(text);
    return error;
}
