#include "winbrand_private.h"

#define FLAG_S_MODE 1
#define FLAG_NOT_S_MODE 2
#define FLAG_INSIDER 4
#define FLAG_NOT_INSIDER 8
#define STRING_S_MODE 4000
#define STRING_INSIDER 4001
#define PRODUCT_CLOUDEDITIONN 202
#define PRODUCT_CLOUDEDITION 203
#define LOAD_DATAFILE 2
#define LOAD_SYSTEM32 0x800
#define PLACEHOLDER_CHARS 260

HINSTANCE g_instance;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_instance = instance;
    return TRUE;
}

static WCHAR *make_path(const WCHAR *folder, const WCHAR *middle, const WCHAR *brand)
{
    SIZE_T chars = wcslen(folder) + wcslen(middle) + 2 * wcslen(brand) + 16;
    WCHAR *path = LocalAlloc(LMEM_FIXED, chars * sizeof(WCHAR));
    if (!path)
        return nullptr;
    wcscpy(path, folder);
    wcscat(path, L"\\Branding\\");
    wcscat(path, brand);
    wcscat(path, middle);
    wcscat(path, brand);
    wcscat(path, L".dll");
    return path;
}

/* the brand's file beside this library when there is one, or else in the windows folder. */
static WCHAR *brand_path(const WCHAR *brand)
{
    WCHAR name[MAX_PATH + 1], folder[MAX_PATH + 4], *file = nullptr;
    if (GetModuleFileNameW(g_instance, name, MAX_PATH) && GetFullPathNameW(name, MAX_PATH + 1, folder, &file) &&
        folder[0] && file) {
        *file = 0;
        WCHAR *path = make_path(folder, L"\\", brand);
        if (path && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
            return path;
        LocalFree(path);
    }
    if (!GetSystemWindowsDirectoryW(folder, MAX_PATH + 1))
        return nullptr;
    WCHAR *path = make_path(folder, L"\\", brand);
    if (path && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return path;
    LocalFree(path);
    return nullptr;
}

static HMODULE brand_load(const WCHAR *brand)
{
    WCHAR *path = brand_path(brand);
    if (!path)
        return nullptr;
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_DATAFILE);
    LocalFree(path);
    return module;
}

static const void *resource(HMODULE module, const WCHAR *type, const WCHAR *name, DWORD *size)
{
    HRSRC found = FindResourceExW(module, type, name, 0);
    HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    const void *data = loaded ? LockResource(loaded) : nullptr;
    if (size)
        *size = data ? SizeofResource(module, found) : 0;
    return data;
}

/* RES_METADATA: version 1, then a type, an id and whether the maker of the computer may replace the resource. */
static BOOL brand_replaceable(HMODULE module, USHORT type, USHORT id)
{
    DWORD size;
    const USHORT *data = resource(module, L"BRANDING_METADATA_RES", L"RES_METADATA", &size);
    if (!data || size < 6 || data[0] != 1 || data[1] != 0)
        return FALSE;
    const USHORT *end = data + size / sizeof(USHORT);
    for (const USHORT *entry = data + 2; entry + 3 <= end && entry[0]; entry += 3)
        if (entry[0] == type && entry[1] == id)
            return entry[2] != 0;
    return FALSE;
}

HMODULE brand_open(const WCHAR *brand, ULONG_PTR type, ULONG_PTR id)
{
    HMODULE module = brand_load(brand);
    if (!module || type >= 0x10000 || id >= 0x10000 || !brand_replaceable(module, (USHORT)type, (USHORT)id))
        return module;
    WCHAR folder[MAX_PATH + 1];
    if (!GetSystemWindowsDirectoryW(folder, MAX_PATH + 1))
        return module;
    WCHAR *path = make_path(folder, L"\\OEM\\", brand);
    HMODULE maker = path ? LoadLibraryExW(path, nullptr, LOAD_DATAFILE) : nullptr;
    LocalFree(path);
    if (!maker)
        return module;
    WCHAR text[PLACEHOLDER_CHARS];
    BOOL has = type == (ULONG_PTR)BRAND_RT_STRING ? LoadStringW(maker, (UINT)id, text, PLACEHOLDER_CHARS) != 0
                                            : FindResourceExW(maker, (const WCHAR *)type, (const WCHAR *)id, 0) != nullptr;
    if (!has) {
        FreeLibrary(maker);
        return module;
    }
    FreeLibrary(module);
    return maker;
}

/*
 * the tables of ids by edition. Basebrd's required one has 18 bytes an edition: the ids of strings 10, 11, 12, 24 and
 * 13, then of pictures 120 and 121. its optional one has 16 bytes: the ids of 16 to 21. Shellbrd's has 8 bytes: the
 * ids of pictures 1050 and 1051.
 */
HRESULT brand_remap(HMODULE module, const WCHAR *brand, UINT id, DWORD edition, UINT *mapped)
{
    static const UCHAR base_slots[][2] = {{10, 0}, {11, 1}, {12, 2}, {24, 3}, {13, 4}, {120, 5}, {121, 6}};
    if (!module || !edition)
        return E_INVALIDARG;
    BOOL base = !_wcsicmp(brand, L"Basebrd");
    if (!base && _wcsicmp(brand, L"Shellbrd"))
        return E_INVALIDARG;
    DWORD size, optional_size;
    const UCHAR *required = resource(module, L"BRANDING_REQUIRED_RESOURCEID_MAP", L"RES_REQUIRED_RESOURCEID_MAP", &size);
    if (!required || !size)
        return HRESULT_FROM_WIN32(GetLastError());
    const UCHAR *optional =
            resource(module, L"BRANDING_OPTIONAL_RESOURCEID_MAP", L"RES_OPTIONAL_RESOURCEID_MAP", &optional_size);
    *mapped = id;
    DWORD width = base ? 18 : 8;
    for (DWORD at = 0; at + width <= size; at += width) {
        const UCHAR *entry = required + at;
        if (*(const DWORD *)entry != edition)
            continue;
        const USHORT *ids = (const USHORT *)(entry + 4);
        if (!base) {
            if (id == 1050 || id == 1051)
                *mapped = ids[id - 1050];
            return S_OK;
        }
        for (unsigned i = 0; i < RTL_NUMBER_OF(base_slots); i++)
            if (id == base_slots[i][0]) {
                *mapped = ids[base_slots[i][1]];
                return S_OK;
            }
        if (optional && id >= 16 && id <= 21)
            for (DWORD other = 0; other + 16 <= optional_size; other += 16)
                if (*(const DWORD *)(optional + other) == edition) {
                    *mapped = ((const USHORT *)(optional + other + 4))[id - 16];
                    break;
                }
        return S_OK;
    }
    return S_OK;
}

DWORD brand_edition(void)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"Kernel-BrandingInfo");
    ULONG type = 0, edition = 0, size = 0;
    if (!NT_SUCCESS(NtQueryLicenseValue(&name, &type, &edition, sizeof(edition), &size)) || type != REG_DWORD)
        return 0;
    return edition;
}

static BOOL edition_is(const WCHAR *wanted)
{
    WCHAR edition[64];
    DWORD size = sizeof(edition);
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key))
        return FALSE;
    LSTATUS status = RegQueryValueExW(key, L"EditionID", nullptr, nullptr, (BYTE *)edition, &size);
    RegCloseKey(key);
    edition[RTL_NUMBER_OF(edition) - 1] = 0;
    return !status && !_wcsicmp(edition, wanted);
}

/* the lockdown mode that wldp tells, or else the editions that only come in S mode. */
static BOOL s_mode(void)
{
    HMODULE wldp = LoadLibraryExW(L"wldp.dll", nullptr, LOAD_SYSTEM32);
    HRESULT(WINAPI * query)(int *) = wldp ? (void *)GetProcAddress(wldp, "WldpQueryWindowsLockdownMode") : nullptr;
    BOOL locked;
    if (query) {
        int mode = 0;
        locked = SUCCEEDED(query(&mode)) && mode > 0;
    } else {
        locked = edition_is(L"Cloud") || edition_is(L"CloudN");
    }
    if (wldp)
        FreeLibrary(wldp);
    return locked;
}

static DWORD current_flags(DWORD flags)
{
    if (!(flags & (FLAG_S_MODE | FLAG_NOT_S_MODE))) {
        DWORD product = 0;
        GetProductInfo(10, 0, 0, 0, &product);
        BOOL cloud = product == PRODUCT_CLOUDEDITIONN || product == PRODUCT_CLOUDEDITION;
        flags |= s_mode() && !cloud ? FLAG_S_MODE : FLAG_NOT_S_MODE;
    }
    if (!(flags & (FLAG_INSIDER | FLAG_NOT_INSIDER)))
        flags |= FLAG_NOT_INSIDER;
    return flags;
}

/* a string of the brand's table, ended and on the heap. nullptr with success left in found when the block lacks it. */
static WCHAR *string_copy(HMODULE module, UINT id, BOOL *found)
{
    *found = FALSE;
    const USHORT *block = resource(module, BRAND_RT_STRING, MAKEINTRESOURCEW(((USHORT)id >> 4) + 1), nullptr);
    if (!block)
        return nullptr;
    for (UINT skip = id & 15; skip; skip--)
        block += *block + 1;
    *found = TRUE;
    USHORT chars = *block;
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, (chars + 1) * sizeof(WCHAR));
    if (!copy)
        return nullptr;
    memcpy(copy, block + 1, chars * sizeof(WCHAR));
    copy[chars] = 0;
    return copy;
}

static void string_free(WCHAR *text)
{
    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

/*
 * the name of an edition takes two inserts, " Insider Preview" then " in S mode", and only strings 12 and 13 are given
 * them. the override of the edition's name by the licence and EditionSubstring is not read: no licence here allows it.
 */
DECLSPEC_EXPORT int WINAPI BrandingLoadStringForEdition(const WCHAR *brand, UINT id, DWORD edition, WCHAR *buffer,
                                                         int count, DWORD flags)
{
    HMODULE module = brand_open(brand, (ULONG_PTR)BRAND_RT_STRING, (USHORT)id);
    if (!module)
        return 0;
    UINT mapped = 0;
    BOOL found = FALSE;
    WCHAR *format = nullptr, *insider = nullptr, *mode = nullptr, *text = nullptr;
    DWORD chars = 0;
    if (FAILED(brand_remap(module, brand, id, edition, &mapped)))
        goto done;
    format = string_copy(module, mapped, &found);
    if (!format)
        goto done;
    flags = current_flags(flags);
    if (id == BRAND_STRING_LONG || id == BRAND_STRING_PRODUCT) {
        if ((flags & FLAG_INSIDER) && !(flags & FLAG_NOT_INSIDER))
            insider = string_copy(module, STRING_INSIDER, &found);
        if ((flags & FLAG_S_MODE) && !(flags & FLAG_NOT_S_MODE))
            mode = string_copy(module, STRING_S_MODE, &found);
    }
    DWORD_PTR inserts[2] = {(DWORD_PTR)(insider ? insider : L""), (DWORD_PTR)(mode ? mode : L"")};
    chars = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                           format, 0, 0, (WCHAR *)&text, 0, (va_list *)inserts);
done:
    string_free(format);
    string_free(insider);
    string_free(mode);
    FreeLibrary(module);
    if (!chars || !text) {
        LocalFree(text);
        return 0;
    }
    int copied = (int)chars < count ? (int)chars : count - 1;
    if (count > 0) {
        memcpy(buffer, text, copied * sizeof(WCHAR));
        buffer[copied] = 0;
    }
    LocalFree(text);
    return copied;
}

DECLSPEC_EXPORT int WINAPI BrandingLoadString(const WCHAR *brand, UINT id, WCHAR *buffer, int count)
{
    return BrandingLoadStringForEdition(brand, id, brand_edition(), buffer, count, 0);
}

/* resource 1 of type 10: an id then an ended string, one after the other, in no language. */
DECLSPEC_EXPORT int WINAPI BrandingLoadNeutralStringForEdition(const WCHAR *brand, UINT id, DWORD edition,
                                                                WCHAR *buffer, int count)
{
    HMODULE module = brand_open(brand, (ULONG_PTR)BRAND_RT_STRING, (USHORT)id);
    if (!module)
        return 0;
    UINT mapped = 0;
    int length = 0;
    const WCHAR *text = nullptr;
    if (SUCCEEDED(brand_remap(module, brand, id, edition, &mapped))) {
        DWORD size;
        const USHORT *data = resource(module, BRAND_RT_RCDATA, MAKEINTRESOURCEW(1), &size);
        const USHORT *end = data ? data + size / sizeof(USHORT) : nullptr;
        while (data && data + 1 < end) {
            const USHORT *start = data + 1, *stop = start;
            while (stop < end && *stop)
                stop++;
            if (stop == end)
                break;
            if (*data == (USHORT)mapped) {
                text = start;
                length = (int)(stop - start);
                break;
            }
            data = stop + 1;
        }
    }
    if (length && count > 0) {
        int copied = length < count ? length : count - 1;
        memcpy(buffer, text, copied * sizeof(WCHAR));
        buffer[copied] = 0;
    }
    FreeLibrary(module);
    return length;
}

static const struct {
    UINT Id;
    const WCHAR *Name;
} g_placeholders[] = {
    {10, L"%WINDOWS_GENERIC%"},
    {11, L"%WINDOWS_SHORT%"},
    {12, L"%WINDOWS_LONG%"},
    {13, L"%WINDOWS_PRODUCT%"},
    {14, L"%WINDOWS_COPYRIGHT%"},
    {15, L"%MICROSOFT_COMPANYNAME%"},
    {22, L"%MICROSOFT_ACCOUNT%"},
    {23, L"%MICROSOFT_ACCOUNTS%"},
    {30, L"%WINDOWS_CLIENT_VERSION_6_1%"},
    {31, L"%WINDOWS_SERVER_VERSION_6_1%"},
    {32, L"%WINDOWS_CLIENT_VERSION_6_2%"},
    {33, L"%WINDOWS_SERVER_VERSION_6_2%"},
    {34, L"%WINDOWS_ARM_VERSION_6_2%"},
    {35, L"%WINDOWS_CLIENT_VERSION_6_3%"},
    {36, L"%WINDOWS_SERVER_VERSION_6_3%"},
    {37, L"%WINDOWS_ARM_VERSION_6_3%"},
    {38, L"%WINDOWS_CLIENT_VERSION_6_4%"},
    {39, L"%WINDOWS_SERVER_VERSION_6_4%"},
    {50, L"%WINDOWS_ARM_VERSION_6_4%"},
    {51, L"%WINDOWS_CLIENT_VERSION_10_0%"},
    {52, L"%WINDOWS_SERVER_VERSION_10_0%"},
    {53, L"%WINDOWS_SERVER_VERSION_10_0_2019_LTSC%"},
    {54, L"%IDS_WINDOWS_SERVER_VERSION_10_0_1809_SAC%"},
    {55, L"%WINDOWS_SERVER_VERSION_10_0_2022_LTSC%"},
    {56, L"%WINDOWS_SERVER_VERSION_10_0_2025_LTSC%"},
};

static int placeholder_at(const WCHAR *text, SIZE_T left)
{
    for (unsigned i = 0; i < RTL_NUMBER_OF(g_placeholders); i++) {
        int chars = (int)wcslen(g_placeholders[i].Name);
        if ((SIZE_T)chars <= left &&
            CompareStringW(LOCALE_USER_DEFAULT, 0, text, chars, g_placeholders[i].Name, chars) == CSTR_EQUAL)
            return (int)i;
    }
    return -1;
}

/*
 * the placeholders of a text replaced by their strings, in memory for GlobalFree. windows sizes the answer from a count
 * of each placeholder alone, which is too small where two of them share a percent sign. here it is the size of what
 * is written.
 */
DECLSPEC_EXPORT WCHAR *WINAPI BrandingFormatStringForEdition(const WCHAR *format, DWORD edition, DWORD flags)
{
    if (!format)
        return nullptr;
    WCHAR(*strings)[PLACEHOLDER_CHARS] =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*strings) * RTL_NUMBER_OF(g_placeholders));
    if (!strings)
        return nullptr;
    BOOLEAN loaded[RTL_NUMBER_OF(g_placeholders)] = {0};
    SIZE_T length = wcslen(format), chars = 0;
    WCHAR *out = nullptr;
    for (int pass = 0; pass < 2; pass++) {
        SIZE_T at = 0;
        for (const WCHAR *p = format; *p;) {
            int which = *p == L'%' ? placeholder_at(p, length - (SIZE_T)(p - format)) : -1;
            if (which < 0) {
                if (out)
                    out[at] = *p;
                at++;
                p++;
                continue;
            }
            if (!loaded[which]) {
                BrandingLoadStringForEdition(L"Basebrd", g_placeholders[which].Id, edition, strings[which],
                                             PLACEHOLDER_CHARS, flags);
                loaded[which] = TRUE;
            }
            SIZE_T size = wcslen(strings[which]);
            if (out)
                memcpy(out + at, strings[which], size * sizeof(WCHAR));
            at += size;
            p += wcslen(g_placeholders[which].Name);
        }
        if (!pass) {
            chars = at;
            out = GlobalAlloc(GPTR, (chars + 1) * sizeof(WCHAR));
            if (!out)
                break;
        }
    }
    HeapFree(GetProcessHeap(), 0, strings);
    return out;
}

DECLSPEC_EXPORT WCHAR *WINAPI BrandingFormatString(const WCHAR *format)
{
    return BrandingFormatStringForEdition(format, brand_edition(), 0);
}

DECLSPEC_EXPORT HMODULE WINAPI GetHinstanceByNameSpace(const WCHAR *brand)
{
    return brand_load(brand);
}
