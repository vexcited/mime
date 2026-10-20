/*
 * the names of application packages: a full name is Name_Version_Architecture_ResourceId_PublisherId, a family name
 * Name_PublisherId. These calls only read the text, they need no package to be installed.
 */
#include "kernelbase_private.h"

/* the structure of a package id, with its strings behind it in the same buffer. */
typedef struct {
    UINT32 Reserved;
    UINT32 ProcessorArchitecture;
    struct {
        USHORT Revision, Build, Minor, Major;
    } Version;
    PWSTR Name;
    PWSTR Publisher;
    PWSTR ResourceId;
    PWSTR PublisherId;
} PACKAGE_ID_;

#define PACKAGE_INFORMATION_FULL 0x100
#define ARCH_X86 0
#define ARCH_ARM 5
#define ARCH_X64 9
#define ARCH_NEUTRAL 11
#define ARCH_ARM64 12
#define ARCH_X86_ON_ARM64 14
#define ARCH_UNKNOWN 0xFFFF
#define NAME_MAX_LENGTH 50
#define NAME_MIN_LENGTH 3
#define RESOURCE_MAX_LENGTH 30
#define PUBLISHER_ID_LENGTH 13
/* the longest full name the parts allow, and what the family name of it needs. */
#define FAMILY_MAX 128

static constexpr WCHAR g_publisher_alphabet[] = L"1234567890abcdefghjkmnpqrstvwxyzABCDEFGHJKMNPQRSTVWXYZ";

/* the names the file system reserves, which no part of a package name may be. */
static BOOL reserved_part(const WCHAR *text, SIZE_T length)
{
    static const PCWSTR names[] = {L".", L"..", L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3", L"com4",
        L"com5", L"com6", L"com7", L"com8", L"com9", L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7",
        L"lpt8", L"lpt9"};
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(names); i++) {
        SIZE_T n = wcslen(names[i]);
        if (length == n && !_wcsnicmp(text, names[i], n))
            return TRUE;
        /* and the same name followed by a dot, which names the device as well. */
        if (length > n && text[n] == L'.' && !_wcsnicmp(text, names[i], n) && i >= 2)
            return TRUE;
    }
    return length >= 4 && !_wcsnicmp(text, L"xn--", 4);
}

static BOOL name_character(WCHAR ch)
{
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-' ||
           ch == L'.';
}

/* the rules both the name and the resource identifier follow once their characters are known good. */
static BOOL valid_part(const WCHAR *text, SIZE_T length)
{
    if (reserved_part(text, length) || text[length - 1] == L'.')
        return FALSE;
    for (SIZE_T i = 0; i + 5 <= length; i++)
        if (!_wcsnicmp(text + i, L".xn--", 5))
            return FALSE;
    return TRUE;
}

/* one number of the version: up to five digits, at most 65535, ended by the dot or by the next underscore. */
static BOOL version_part(const WCHAR **cursor, WCHAR end, USHORT *value)
{
    const WCHAR *p = *cursor;
    UINT32 number = 0;
    SIZE_T digits = 0;
    while (*p != end) {
        if (*p < L'0' || *p > L'9' || ++digits > 5)
            return FALSE;
        number = number * 10 + (UINT32)(*p - L'0');
        p++;
    }
    if (!digits || number > 0xFFFF)
        return FALSE;
    *value = (USHORT)number;
    *cursor = p + 1;
    return TRUE;
}

/*
 * splits a full name into the parts of a package id. The strings are copied behind the structure of the buffer, whose
 * size is *length on the way in and what it needs on the way out. Returns a win32 error.
 */
static LONG parse_full_name(PCWSTR full, UINT32 *length, BYTE *buffer, PACKAGE_ID_ *out)
{
    if (!full || !length || (*length && !buffer))
        return ERROR_INVALID_PARAMETER;
    const WCHAR *name = full, *p = full;
    while (*p != L'_') {
        if (!*p || (SIZE_T)(p - name) >= NAME_MAX_LENGTH || !name_character(*p))
            return ERROR_INVALID_PARAMETER;
        p++;
    }
    SIZE_T name_length = (SIZE_T)(p - name);
    if (name_length < NAME_MIN_LENGTH || !valid_part(name, name_length))
        return ERROR_INVALID_PARAMETER;

    PACKAGE_ID_ id = {};
    p++;
    if (!version_part(&p, L'.', &id.Version.Major) || !version_part(&p, L'.', &id.Version.Minor) ||
        !version_part(&p, L'.', &id.Version.Build) || !version_part(&p, L'_', &id.Version.Revision))
        return ERROR_INVALID_PARAMETER;

    const WCHAR *architecture = p;
    while (*p != L'_') {
        if (!*p || (SIZE_T)(p - architecture) > 7)
            return ERROR_INVALID_PARAMETER;
        p++;
    }
    SIZE_T architecture_length = (SIZE_T)(p - architecture);
    static const struct {
        PCWSTR Text;
        UINT32 Value;
    } architectures[] = {{L"x86", ARCH_X86}, {L"arm", ARCH_ARM}, {L"x64", ARCH_X64}, {L"arm64", ARCH_ARM64},
                         {L"x86a64", ARCH_X86_ON_ARM64}, {L"neutral", ARCH_NEUTRAL}, {L"unknown", ARCH_UNKNOWN}};
    BOOL known = FALSE;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(architectures) && !known; i++) {
        SIZE_T n = wcslen(architectures[i].Text);
        if (n == architecture_length && !_wcsnicmp(architecture, architectures[i].Text, n)) {
            id.ProcessorArchitecture = architectures[i].Value;
            known = TRUE;
        }
    }
    if (!known)
        return ERROR_INVALID_PARAMETER;

    /* the resource identifier is empty when it is a tilde, and then the package has no architecture. */
    p++;
    const WCHAR *resource = p;
    BOOL neutral_resource = *p == L'~';
    SIZE_T resource_length = 0;
    if (neutral_resource) {
        p++;
    } else {
        while (*p != L'_') {
            if (!*p || resource_length >= RESOURCE_MAX_LENGTH || !name_character(*p))
                return ERROR_INVALID_PARAMETER;
            resource_length++;
            p++;
        }
        if (resource_length && !valid_part(resource, resource_length))
            return ERROR_INVALID_PARAMETER;
    }
    if (*p != L'_')
        return ERROR_INVALID_PARAMETER;
    if (neutral_resource && id.ProcessorArchitecture != ARCH_NEUTRAL)
        return ERROR_INVALID_PARAMETER;

    p++;
    const WCHAR *publisher = p;
    SIZE_T publisher_length = 0;
    for (; *p; p++, publisher_length++) {
        BOOL good = FALSE;
        for (SIZE_T i = 0; g_publisher_alphabet[i]; i++)
            good |= g_publisher_alphabet[i] == *p;
        if (!good || publisher_length >= PUBLISHER_ID_LENGTH)
            return ERROR_INVALID_PARAMETER;
    }
    if (publisher_length != PUBLISHER_ID_LENGTH)
        return ERROR_INVALID_PARAMETER;

    UINT32 needed = (UINT32)(sizeof(PACKAGE_ID_) + 2 * (name_length + 1 + resource_length + 1 + PUBLISHER_ID_LENGTH + 1));
    if (*length < needed) {
        *length = needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    PWSTR text = (PWSTR)(buffer + sizeof(PACKAGE_ID_));
    id.Name = text;
    memcpy(text, name, name_length * sizeof(WCHAR));
    text += name_length;
    *text++ = 0;
    id.ResourceId = text;
    memcpy(text, resource, resource_length * sizeof(WCHAR));
    text += resource_length;
    *text++ = 0;
    id.PublisherId = text;
    memcpy(text, publisher, PUBLISHER_ID_LENGTH * sizeof(WCHAR));
    text[PUBLISHER_ID_LENGTH] = 0;
    *length = needed;
    memcpy(buffer, &id, sizeof(id));
    if (out)
        *out = id;
    return ERROR_SUCCESS;
}

/* the id of a package from its full name; the full form needs the package to be installed, and none is. */
DECLSPEC_EXPORT LONG WINAPI PackageIdFromFullName(PCWSTR full, UINT32 flags, UINT32 *length, BYTE *buffer)
{
    if (flags & PACKAGE_INFORMATION_FULL)
        return APPMODEL_ERROR_NO_PACKAGE;
    return parse_full_name(full, length, buffer, nullptr);
}

DECLSPEC_EXPORT LONG WINAPI PackageFamilyNameFromFullName(PCWSTR full, UINT32 *length, PWSTR family)
{
    BYTE parts[sizeof(PACKAGE_ID_) + 2 * (NAME_MAX_LENGTH + 1 + RESOURCE_MAX_LENGTH + 1 + PUBLISHER_ID_LENGTH + 1)];
    UINT32 size = sizeof(parts);
    PACKAGE_ID_ id;
    LONG error = parse_full_name(full, &size, parts, &id);
    if (error)
        return error;
    if (!length || (*length && !family))
        return ERROR_INVALID_PARAMETER;
    SIZE_T name_length = wcslen(id.Name);
    UINT32 needed = (UINT32)(name_length + 1 + PUBLISHER_ID_LENGTH + 1);
    if (*length < needed) {
        *length = needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy(family, id.Name, name_length * sizeof(WCHAR));
    family[name_length] = L'_';
    memcpy(family + name_length + 1, id.PublisherId, (PUBLISHER_ID_LENGTH + 1) * sizeof(WCHAR));
    *length = needed;
    return ERROR_SUCCESS;
}
