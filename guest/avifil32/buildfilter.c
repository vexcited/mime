/*
 * AVIBuildFilter: the filter of a file dialog for the files the AVIFile handlers read or write, built from the
 * extensions the registry names below AVIFile\Extensions. Each handler is listed once with the extensions that
 * lead to it, after "All multimedia files" and before "All Files".
 */
#include "avifil32_private.h"

#define FILTER_ENTRIES 256
#define ID_FIELD 64
#define EXT_FIELD 128

typedef struct FilterEntry {
    WCHAR clsid[ID_FIELD];
    WCHAR extensions[EXT_FIELD];
} FilterEntry;

static void append_to(WCHAR *dest, SIZE_T capacity, const WCHAR *text)
{
    SIZE_T at = lstrlenW(dest);
    AviCopyName(dest + at, text, capacity - at);
}

static BOOL default_value(HKEY root, const WCHAR *path, WCHAR *value, DWORD capacity)
{
    HKEY key;
    DWORD type, size = capacity * sizeof(WCHAR) - sizeof(WCHAR);
    BOOL ok;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return FALSE;
    value[0] = 0;
    ok = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)value, &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    if (ok)
        value[size / sizeof(WCHAR)] = 0;
    return ok;
}

HRESULT WINAPI AVIBuildFilterW(LPWSTR filter, LONG size, BOOL saving)
{
    static const WCHAR all_files[] = L"All Files (*.*)";
    static const WCHAR all_files_pattern[] = L"*.*";
    static const WCHAR all_media[] = L"All multimedia files";
    FilterEntry *entries = AviAlloc(sizeof(FilterEntry) * (FILTER_ENTRIES + 1));
    HKEY extensions;
    LONG count = 1;
    LONG room = size;
    WCHAR *out = filter;
    if (!entries)
        return AVIERR_MEMORY;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"AVIFile\\Extensions", 0, KEY_READ, &extensions) != ERROR_SUCCESS) {
        AviFree(entries);
        return AVIERR_ERROR;
    }
    for (DWORD index = 0;; index++) {
        WCHAR name[12], clsid[128], pattern[16], capabilities[16];
        DWORD name_size = 12, type, value_size = sizeof(clsid) - sizeof(WCHAR);
        LONG found = 1;
        if (RegEnumKeyExW(extensions, index, name, &name_size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY key;
        clsid[0] = 0;
        if (RegOpenKeyExW(extensions, name, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            break;
        BOOL ok = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)clsid, &value_size) == ERROR_SUCCESS;
        RegCloseKey(key);
        if (!ok)
            break;
        clsid[value_size / sizeof(WCHAR)] = 0;
        clsid[ID_FIELD - 1] = 0;
        while (found < count && lstrcmpW(clsid, entries[found].clsid))
            found++;
        if (found == count) {
            /* a handler that cannot read, or cannot write, is not listed for it. */
            WCHAR path[128] = L"CLSID\\";
            if (count == FILTER_ENTRIES)
                continue;
            append_to(path, 128, clsid);
            append_to(path, 128, L"\\AVIFile");
            if (default_value(HKEY_CLASSES_ROOT, path, capabilities, 16)) {
                DWORD flags = 0;
                for (const WCHAR *c = capabilities; *c >= '0' && *c <= '9'; c++)
                    flags = flags * 10 + (DWORD)(*c - '0');
                if (saving ? !(flags & 2) : !(flags & 1))
                    continue;
            }
            AviCopyName(entries[count].clsid, clsid, ID_FIELD);
            count++;
        }
        pattern[0] = ';';
        pattern[1] = '*';
        pattern[2] = '.';
        AviCopyName(pattern + 3, name, 13);
        append_to(entries[found].extensions, EXT_FIELD, entries[found].extensions[0] ? pattern : pattern + 1);
        append_to(entries[0].extensions, EXT_FIELD, entries[0].extensions[0] ? pattern : pattern + 1);
    }
    RegCloseKey(extensions);
    HKEY classes;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &classes) != ERROR_SUCCESS) {
        AviFree(entries);
        return AVIERR_ERROR;
    }
    for (LONG k = 0; k < count && room > 0; k++) {
        WCHAR name[256];
        if (k) {
            if (!default_value(classes, entries[k].clsid, name, 256))
                continue;
        } else {
            AviCopyName(name, all_media, 256);
        }
        /* a handler is skipped when it does not fit, the first entry is cut short instead. */
        LONG need = lstrlenW(name) + lstrlenW(entries[k].extensions) + 2;
        if (k && room < need + 8)
            break;
        AviCopyName(out, name, (SIZE_T)room);
        out += lstrlenW(out) + 1;
        room = size - (LONG)(out - filter);
        if (room <= 0) {
            RegCloseKey(classes);
            AviFree(entries);
            return AVIERR_BUFFERTOOSMALL;
        }
        AviCopyName(out, entries[k].extensions, (SIZE_T)room);
        out += lstrlenW(out) + 1;
        room = size - (LONG)(out - filter);
        if (room <= 0) {
            RegCloseKey(classes);
            AviFree(entries);
            return AVIERR_BUFFERTOOSMALL;
        }
    }
    RegCloseKey(classes);
    AviFree(entries);
    {
        LONG tail = (LONG)lstrlenW(all_files) + 1 + (LONG)lstrlenW(all_files_pattern) + 1;
        if (room >= tail + 1) {
            AviCopyName(out, all_files, (SIZE_T)room);
            out += lstrlenW(out) + 1;
            AviCopyName(out, all_files_pattern, (SIZE_T)room);
            out += lstrlenW(out) + 1;
            *out = 0;
        }
    }
    return S_OK;
}

HRESULT WINAPI AVIBuildFilterA(LPSTR filter, LONG size, BOOL saving)
{
    if (size < 0 || (SIZE_T)size > 0x3FFFFFFF)
        return (HRESULT)0x80070216L;
    WCHAR *wide = AviAlloc((SIZE_T)(size ? size : 1) * sizeof(WCHAR));
    if (!wide)
        return AVIERR_MEMORY;
    HRESULT hr = AVIBuildFilterW(wide, size, saving);
    if (hr >= 0) {
        LONG used = 0;
        const WCHAR *at = wide;
        for (int length = lstrlenW(at); length > 0; length = lstrlenW(at)) {
            int count = WideCharToMultiByte(CP_ACP, 0, at, -1, filter, size - used - 1, nullptr, nullptr);
            used += count;
            filter += count;
            at += length + 1;
            if (used >= size - 1)
                break;
        }
        *filter = 0;
    }
    AviFree(wide);
    return hr;
}
