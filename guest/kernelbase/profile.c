#include "kernelbase_private.h"

#define PROFILE_LIMIT (16 * 1024 * 1024)

typedef struct {
    PWSTR Text;
    SIZE_T Length;
    BOOL Unicode;
} PROFILE;

typedef struct {
    SIZE_T Start, Next, Name, NameEnd, Value, ValueEnd;
    BOOL Section, Key, Equals;
} PROFILE_LINE;

static volatile LONG temporary_id;

static BOOL space(WCHAR c)
{
    return c == L' ' || c == L'\t';
}

static BOOL contains(PCWSTR text, PCWSTR characters)
{
    while (*text) {
        if (wcschr(characters, *text++))
            return TRUE;
    }
    return FALSE;
}

static PWSTR profile_path(PCWSTR name)
{
    if (!name)
        name = L"win.ini";
    if (!*name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    SIZE_T length = wcslen(name);
    if (length > 32700) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return nullptr;
    }
    /* only a bare file name is in the windows directory, a name with a directory resolves from the current one. */
    if (contains(name, L"\\/") || (length > 1 && name[1] == L':')) {
        DWORD needed = GetFullPathNameW(name, 0, nullptr, nullptr);
        PWSTR path = needed ? KbAlloc(needed * sizeof(WCHAR)) : nullptr;
        if (!path) {
            if (needed)
                KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        DWORD written = GetFullPathNameW(name, needed, path, nullptr);
        if (!written || written >= needed) {
            KbFree(path);
            return nullptr;
        }
        return path;
    }
    PWSTR path = KbAlloc((length + MAX_PATH + 2) * sizeof(WCHAR));
    if (!path) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    UINT count = GetWindowsDirectoryW(path, MAX_PATH);
    if (!count || count >= MAX_PATH) {
        KbFree(path);
        return nullptr;
    }
    path[count++] = L'\\';
    wcscpy(path + count, name);
    return path;
}


/*
 * win.ini, system.ini and the other files named under IniFileMapping do not keep their entries in the file: the key of
 * the file says where in the registry each section, or each key of a section, lives. a section of a file has a subkey
 * of that name, whose value for the key (or, failing that, its default value) is the place, or a value of the file's key
 * named like the section does, or the default value of the file's key. the place is "USR:" and a path under the current
 * user, "SYS:" and a path under HKLM\SOFTWARE, with ! to write the file too and @ to never read it. what the registry
 * does not have is read from the file.
 */
typedef struct {
    HKEY Root;
    WCHAR Path[512];
    BOOL Both, NoFile;
} PROFILE_MAP;

static BOOL map_string(HKEY key, PCWSTR name, PWSTR out, DWORD chars)
{
    DWORD type, size = (chars - 1) * sizeof(WCHAR);
    if (RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)out, &size) != ERROR_SUCCESS || type != REG_SZ)
        return FALSE;
    out[size / sizeof(WCHAR)] = 0;
    return out[0] != 0;
}

static BOOL map_place(PWSTR text, PROFILE_MAP *map)
{
    map->Both = map->NoFile = FALSE;
    for (;; text++) {
        if (*text == L'!')
            map->Both = TRUE;
        else if (*text == L'@')
            map->NoFile = TRUE;
        else if (*text != L'#')
            break;
    }
    PCWSTR prefix = L"SOFTWARE\\";
    if (!_wcsnicmp(text, L"USR:", 4)) {
        map->Root = HKEY_CURRENT_USER;
        text += 4;
        prefix = L"";
    } else if (!_wcsnicmp(text, L"SYS:", 4)) {
        map->Root = HKEY_LOCAL_MACHINE;
        text += 4;
    } else {
        return FALSE;
    }
    /* the places have doubled separators in some entries. */
    SIZE_T n = wcslen(prefix);
    memcpy(map->Path, prefix, n * sizeof(WCHAR));
    for (; *text && n + 1 < RTL_NUMBER_OF(map->Path); text++)
        if (*text != L'\\' || n == 0 || map->Path[n - 1] != L'\\')
            map->Path[n++] = *text;
    map->Path[n] = 0;
    return TRUE;
}

static BOOL find_map(PCWSTR filename, PCWSTR section, PCWSTR key, PROFILE_MAP *map)
{
    if (!section || !key)
        return FALSE;
    PCWSTR leaf = filename ? filename : L"win.ini";
    PCWSTR slash = wcsrchr(leaf, L'\\');
    if (slash) {
        WCHAR windows[MAX_PATH];
        UINT n = GetWindowsDirectoryW(windows, RTL_NUMBER_OF(windows));
        if (!n || n >= RTL_NUMBER_OF(windows) || (SIZE_T)(slash - leaf) != n || _wcsnicmp(leaf, windows, n))
            return FALSE;
        leaf = slash + 1;
    }
    if (!*leaf || wcslen(leaf) > 64)
        return FALSE;
    WCHAR path[200], place[512];
    KbFormat(path, RTL_NUMBER_OF(path), L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\IniFileMapping\\%s", leaf);
    HKEY file;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &file) != ERROR_SUCCESS)
        return FALSE;
    BOOL found = FALSE;
    HKEY sub;
    if (wcslen(section) < 100 && RegOpenKeyExW(file, section, 0, KEY_READ, &sub) == ERROR_SUCCESS) {
        found = map_string(sub, key, place, RTL_NUMBER_OF(place)) || map_string(sub, nullptr, place, RTL_NUMBER_OF(place));
        RegCloseKey(sub);
    }
    if (!found)
        found = map_string(file, section, place, RTL_NUMBER_OF(place)) || map_string(file, nullptr, place, RTL_NUMBER_OF(place));
    RegCloseKey(file);
    return found && map_place(place, map);
}

/* reads a mapped entry: TRUE when the registry has it. */
static BOOL read_mapped(const PROFILE_MAP *map, PCWSTR key, PWSTR text, DWORD chars)
{
    HKEY hkey;
    if (RegOpenKeyExW(map->Root, map->Path, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return FALSE;
    DWORD type, size = (chars - 1) * sizeof(WCHAR);
    BYTE data[2048];
    BOOL ok = FALSE;
    DWORD capacity = size < sizeof(data) ? size : (DWORD)sizeof(data);
    DWORD got = capacity;
    if (RegQueryValueExW(hkey, key, nullptr, &type, data, &got) == ERROR_SUCCESS) {
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            memcpy(text, data, got);
            text[got / sizeof(WCHAR)] = 0;
            ok = TRUE;
        } else if (type == REG_DWORD && got >= sizeof(DWORD)) {
            KbFormat(text, chars, L"%u", *(DWORD *)data);
            ok = TRUE;
        }
    }
    RegCloseKey(hkey);
    return ok;
}

static BOOL load_profile(PCWSTR path, PROFILE *profile, BOOL missing_ok)
{
    memset(profile, 0, sizeof(*profile));
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (!missing_ok || GetLastError() != ERROR_FILE_NOT_FOUND)
            return FALSE;
        profile->Text = KbAllocZero(sizeof(WCHAR));
        if (!profile->Text)
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return profile->Text != nullptr;
    }
    LARGE_INTEGER size;
    BOOL ok = GetFileSizeEx(file, &size);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (ok && (size.QuadPart < 0 || size.QuadPart > PROFILE_LIMIT)) {
        error = ERROR_FILE_TOO_LARGE;
        ok = FALSE;
    }
    BYTE *bytes = ok ? KbAlloc((SIZE_T)size.QuadPart + 2) : nullptr;
    if (ok && !bytes) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        ok = FALSE;
    }
    DWORD count = 0;
    if (ok && (!ReadFile(file, bytes, (DWORD)size.QuadPart, &count, nullptr) || count != size.QuadPart)) {
        error = count != size.QuadPart ? ERROR_READ_FAULT : GetLastError();
        ok = FALSE;
    }
    CloseHandle(file);
    if (ok) {
        profile->Unicode = count >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe;
        if (profile->Unicode && (count & 1)) {
            error = ERROR_INVALID_DATA;
            ok = FALSE;
        }
    }
    if (ok) {
        SIZE_T length = profile->Unicode ? (count - 2) / 2 :
                        count ? (SIZE_T)MultiByteToWideChar(CP_ACP, 0, (PCSTR)bytes, count, nullptr, 0) : 0;
        if (count && !profile->Unicode && !length) {
            error = GetLastError();
            ok = FALSE;
        } else {
            profile->Text = KbAlloc((length + 1) * sizeof(WCHAR));
            if (!profile->Text) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                ok = FALSE;
            } else {
                if (profile->Unicode)
                    memcpy(profile->Text, bytes + 2, length * sizeof(WCHAR));
                else if (length)
                    MultiByteToWideChar(CP_ACP, 0, (PCSTR)bytes, count, profile->Text, (int)length);
                profile->Text[length] = 0;
                profile->Length = length;
                for (SIZE_T i = 0; i < length; i++) {
                    if (!profile->Text[i]) {
                        error = ERROR_INVALID_DATA;
                        ok = FALSE;
                        break;
                    }
                }
            }
        }
    }
    KbFree(bytes);
    if (!ok) {
        KbFree(profile->Text);
        profile->Text = nullptr;
        KbSetError(error);
    }
    return ok;
}

static PROFILE_LINE next_line(const PROFILE *profile, SIZE_T at)
{
    PCWSTR text = profile->Text;
    PROFILE_LINE line = {.Start = at};
    SIZE_T end = at;
    while (end < profile->Length && text[end] != L'\r' && text[end] != L'\n')
        end++;
    line.Next = end;
    if (line.Next < profile->Length && text[line.Next] == L'\r')
        line.Next++;
    if (line.Next < profile->Length && text[line.Next] == L'\n')
        line.Next++;
    while (at < end && space(text[at]))
        at++;
    while (end > at && space(text[end - 1]))
        end--;
    if (at == end || text[at] == L';')
        return line;
    if (text[at] == L'[') {
        SIZE_T close = ++at;
        while (close < end && text[close] != L']')
            close++;
        if (close == end)
            return line;
        end = close;
        while (at < end && space(text[at]))
            at++;
        while (end > at && space(text[end - 1]))
            end--;
        line.Section = TRUE;
        line.Name = at;
        line.NameEnd = end;
        return line;
    }
    SIZE_T equals = at;
    while (equals < end && text[equals] != L'=')
        equals++;
    line.Name = at;
    line.NameEnd = equals;
    while (line.NameEnd > at && space(text[line.NameEnd - 1]))
        line.NameEnd--;
    line.Value = equals < end ? equals + 1 : end;
    while (line.Value < end && space(text[line.Value]))
        line.Value++;
    line.ValueEnd = end;
    line.Key = line.NameEnd > line.Name;
    line.Equals = equals < end;
    return line;
}

static BOOL matches(PCWSTR text, SIZE_T start, SIZE_T end, PCWSTR name)
{
    return wcslen(name) == end - start && !_wcsnicmp(text + start, name, end - start);
}

static DWORD copy_value(PCWSTR text, SIZE_T length, PWSTR out, DWORD size)
{
    if (length >= size)
        length = size - 1;
    memcpy(out, text, length * sizeof(WCHAR));
    out[length] = 0;
    return (DWORD)length;
}

DWORD WINAPI GetPrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR out, DWORD size,
                                      LPCWSTR filename)
{
    if (!size)
        return 0;
    if (!out) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    out[0] = 0;
    BOOL multiple = !section || !key;
    if (multiple && size > 1)
        out[1] = 0;
    PROFILE_MAP map;
    WCHAR mapped[512];
    if (!multiple && find_map(filename, section, key, &map) && read_mapped(&map, key, mapped, RTL_NUMBER_OF(mapped))) {
        SIZE_T length = wcslen(mapped);
        DWORD used = copy_value(mapped, length, out, size);
        KbSetError(length >= size ? ERROR_MORE_DATA : ERROR_SUCCESS);
        return used;
    }
    PWSTR path = profile_path(filename);
    PROFILE profile = {};
    BOOL loaded = path && load_profile(path, &profile, FALSE);
    DWORD error = loaded ? ERROR_SUCCESS : GetLastError(), used = 0;
    BOOL active = FALSE, found_section = FALSE, found_value = FALSE;
    if (loaded) {
        for (SIZE_T at = 0; at < profile.Length;) {
            PROFILE_LINE line = next_line(&profile, at);
            at = line.Next;
            BOOL emit = FALSE;
            if (line.Section) {
                if (section && found_section)
                    break;
                active = section && matches(profile.Text, line.Name, line.NameEnd, section);
                found_section |= active;
                emit = !section;
            } else if (line.Key && active) {
                if (key && matches(profile.Text, line.Name, line.NameEnd, key)) {
                    SIZE_T begin = line.Value, end = line.ValueEnd;
                    if (end - begin >= 2 && (profile.Text[begin] == L'\'' || profile.Text[begin] == L'"') &&
                        profile.Text[end - 1] == profile.Text[begin]) {
                        begin++;
                        end--;
                    }
                    if (end - begin >= size)
                        error = ERROR_MORE_DATA;
                    used = copy_value(profile.Text + begin, end - begin, out, size);
                    found_value = TRUE;
                    break;
                }
                emit = !key;
            }
            if (!emit || line.Name == line.NameEnd)
                continue;
            SIZE_T length = line.NameEnd - line.Name;
            if ((SIZE_T)used + length + 2 > size) {
                if (size > 1) {
                    DWORD available = used < size - 2 ? size - 2 - used : 0;
                    memcpy(out + used, profile.Text + line.Name, available * sizeof(WCHAR));
                    out[size - 2] = out[size - 1] = 0;
                }
                used = size > 1 ? size - 2 : 0;
                error = ERROR_MORE_DATA;
                break;
            }
            memcpy(out + used, profile.Text + line.Name, length * sizeof(WCHAR));
            used += (DWORD)length;
            out[used++] = 0;
            out[used] = 0;
        }
    }
    if (!multiple && !found_value) {
        if (!fallback)
            fallback = L"";
        SIZE_T length = wcslen(fallback);
        while (length && fallback[length - 1] == L' ')
            length--;
        used = copy_value(fallback, length, out, size);
    }
    KbFree(profile.Text);
    KbFree(path);
    KbSetError(error);
    return used;
}

UINT WINAPI GetPrivateProfileIntW(LPCWSTR section, LPCWSTR key, INT fallback, LPCWSTR filename)
{
    WCHAR text[64];
    if (!section || !key || !GetPrivateProfileStringW(section, key, L"", text, RTL_NUMBER_OF(text), filename))
        return (UINT)fallback;
    UNICODE_STRING value;
    RtlInitUnicodeString(&value, text);
    ULONG result = 0;
    RtlUnicodeStringToInteger(&value, 0, &result);
    return result;
}

static BOOL save_profile(PCWSTR path, PCWSTR text, SIZE_T length, BOOL unicode)
{
    int encoded = unicode ? 0 : length ? WideCharToMultiByte(CP_ACP, 0, text, (int)length, nullptr, 0, nullptr, nullptr) : 0;
    if (!unicode && length && !encoded)
        return FALSE;
    SIZE_T size = unicode ? (length + 1) * sizeof(WCHAR) : (SIZE_T)encoded;
    if (size > PROFILE_LIMIT) {
        KbSetError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    BYTE *bytes = KbAlloc(size ? size : 1);
    SIZE_T path_size = wcslen(path) + 64;
    PWSTR temporary = KbAlloc(path_size * sizeof(WCHAR));
    if (!bytes || !temporary) {
        KbFree(bytes);
        KbFree(temporary);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (unicode) {
        bytes[0] = 0xff;
        bytes[1] = 0xfe;
        memcpy(bytes + 2, text, length * sizeof(WCHAR));
    } else if (size)
        WideCharToMultiByte(CP_ACP, 0, text, (int)length, (PSTR)bytes, encoded, nullptr, nullptr);
    HANDLE file = INVALID_HANDLE_VALUE;
    for (ULONG i = 0; i < 32 && file == INVALID_HANDLE_VALUE; i++) {
        KbFormat(temporary, path_size, L"%s.%u.%u.tmp", path, GetCurrentProcessId(),
                 (ULONG)InterlockedIncrement(&temporary_id));
        file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS)
            break;
    }
    BOOL ok = file != INVALID_HANDLE_VALUE;
    DWORD error = ok ? ERROR_SUCCESS : GetLastError(), written;
    if (ok) {
        ok = WriteFile(file, bytes, (DWORD)size, &written, nullptr);
        if (!ok)
            error = GetLastError();
        else if (written != size) {
            error = ERROR_WRITE_FAULT;
            ok = FALSE;
        } else if (!FlushFileBuffers(file)) {
            error = GetLastError();
            ok = FALSE;
        }
        CloseHandle(file);
        if (ok && !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = GetLastError();
            ok = FALSE;
        }
        if (!ok)
            DeleteFileW(temporary);
    }
    KbFree(bytes);
    KbFree(temporary);
    KbSetError(error);
    return ok;
}

static BOOL edit_profile(PCWSTR section, PCWSTR key, PCWSTR value, PCWSTR path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))) {
        KbSetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    PROFILE profile;
    if (!load_profile(path, &profile, TRUE))
        return FALSE;
    SIZE_T start = profile.Length, end = start, section_end = start;
    BOOL active = FALSE, found_section = FALSE, found_key = FALSE;
    for (SIZE_T at = 0; at < profile.Length;) {
        PROFILE_LINE line = next_line(&profile, at);
        at = line.Next;
        if (line.Section) {
            if (found_section) {
                section_end = line.Start;
                if (!key)
                    end = section_end;
                break;
            }
            active = matches(profile.Text, line.Name, line.NameEnd, section);
            if (active) {
                found_section = TRUE;
                if (!key) {
                    start = line.Start;
                    end = profile.Length;
                }
            }
        } else if (active && key && line.Key && !found_key && matches(profile.Text, line.Name, line.NameEnd, key)) {
            found_key = TRUE;
            start = line.Start;
            end = line.Next;
        }
    }
    if ((!key && !found_section) || (key && !value && !found_key)) {
        KbFree(profile.Text);
        KbSetError(ERROR_SUCCESS);
        return TRUE;
    }
    BOOL append = key && value && !found_key;
    if (append)
        start = end = found_section ? section_end : profile.Length;
    SIZE_T section_length = wcslen(section), key_length = key ? wcslen(key) : 0, value_length = value ? wcslen(value) : 0;
    SIZE_T capacity = profile.Length + section_length + key_length + value_length + 16;
    PWSTR output = capacity <= PROFILE_LIMIT ? KbAlloc(capacity * sizeof(WCHAR)) : nullptr;
    if (!output) {
        KbFree(profile.Text);
        KbSetError(capacity > PROFILE_LIMIT ? ERROR_FILE_TOO_LARGE : ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(output, profile.Text, start * sizeof(WCHAR));
    SIZE_T used = start;
    if (key && value) {
        if (used && output[used - 1] != L'\n' && output[used - 1] != L'\r') {
            output[used++] = L'\r';
            output[used++] = L'\n';
        }
        if (!found_section) {
            output[used++] = L'[';
            memcpy(output + used, section, section_length * sizeof(WCHAR));
            used += section_length;
            output[used++] = L']';
            output[used++] = L'\r';
            output[used++] = L'\n';
        }
        memcpy(output + used, key, key_length * sizeof(WCHAR));
        used += key_length;
        output[used++] = L'=';
        memcpy(output + used, value, value_length * sizeof(WCHAR));
        used += value_length;
        output[used++] = L'\r';
        output[used++] = L'\n';
    }
    memcpy(output + used, profile.Text + end, (profile.Length - end) * sizeof(WCHAR));
    used += profile.Length - end;
    BOOL result = save_profile(path, output, used, profile.Unicode);
    KbFree(output);
    KbFree(profile.Text);
    return result;
}

/* the profile mutex is the lock every writer in every process takes. */
static BOOL write_locked(PCWSTR filename, BOOL (*writer)(const void *, PCWSTR), const void *arguments)
{
    PWSTR path = profile_path(filename);
    if (!path)
        return FALSE;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\{3f5c1e8a-2b4d-4c9a-9e1f-6b7d8c5a4e2f}");
    BOOL result = FALSE;
    if (mutex) {
        DWORD wait = WaitForSingleObject(mutex, INFINITE);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED_0) {
            result = writer(arguments, path);
            DWORD error = GetLastError();
            ReleaseMutex(mutex);
            KbSetError(error);
        }
    }
    DWORD error = GetLastError();
    if (mutex)
        CloseHandle(mutex);
    KbFree(path);
    KbSetError(error);
    return result;
}

typedef struct {
    PCWSTR Section, Key, Value;
} STRING_EDIT;

static BOOL write_string(const void *arguments, PCWSTR path)
{
    const STRING_EDIT *edit = arguments;
    return edit_profile(edit->Section, edit->Key, edit->Value, path);
}

BOOL WINAPI WritePrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR filename)
{
    if (!section && !key && !value) {
        KbSetError(ERROR_SUCCESS);
        return FALSE;
    }
    if (!section || !*section || contains(section, L"\r\n]") ||
        (key && (!*key || contains(key, L"\r\n="))) || (value && contains(value, L"\r\n"))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (wcslen(section) > PROFILE_LIMIT || (key && wcslen(key) > PROFILE_LIMIT) ||
        (value && wcslen(value) > PROFILE_LIMIT)) {
        KbSetError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    PROFILE_MAP map;
    if (find_map(filename, section, key, &map)) {
        HKEY hkey;
        LSTATUS status = RegCreateKeyExW(map.Root, map.Path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hkey, nullptr);
        if (status == ERROR_SUCCESS) {
            status = value ? RegSetValueExW(hkey, key, 0, REG_SZ, (const BYTE *)value,
                                            (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)))
                           : RegDeleteValueW(hkey, key);
            RegCloseKey(hkey);
            if (!value && status == ERROR_FILE_NOT_FOUND)
                status = ERROR_SUCCESS;
        }
        if (status != ERROR_SUCCESS) {
            KbSetError(status);
            return FALSE;
        }
        if (!map.Both) {
            KbSetError(ERROR_SUCCESS);
            return TRUE;
        }
    }
    STRING_EDIT edit = {section, key, value};
    return write_locked(filename, write_string, &edit);
}


/* ------------------------------------------------------------------ sections */

/*
 * the keys as "key=value" strings ended by an empty one: the key without trailing blanks, the value without leading
 * blanks, comments and blank lines skipped. A line with no equals sign is returned as is, which dxupdate needs.
 */
DWORD WINAPI GetPrivateProfileSectionW(LPCWSTR section, LPWSTR out, DWORD size, LPCWSTR filename)
{
    if (!section || !out) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!size)
        return 0;
    out[0] = 0;
    if (size > 1)
        out[1] = 0;
    PWSTR path = profile_path(filename);
    PROFILE profile = {};
    BOOL loaded = path && load_profile(path, &profile, FALSE);
    DWORD error = loaded ? ERROR_SUCCESS : GetLastError();
    /* fill all but the last two characters, which end the list. */
    DWORD room = size > 1 ? size - 2 : 0, used = 0;
    BOOL truncated = FALSE, active = FALSE, found = FALSE;
    for (SIZE_T at = 0; loaded && at < profile.Length && !truncated;) {
        PROFILE_LINE line = next_line(&profile, at);
        at = line.Next;
        if (line.Section) {
            if (found)
                break;
            active = matches(profile.Text, line.Name, line.NameEnd, section);
            found |= active;
        } else if (line.Key && active) {
            SIZE_T key = line.NameEnd - line.Name, value = line.ValueEnd - line.Value;
            BOOL equals = line.Equals;
            SIZE_T entry = key + (equals ? 1 + value : 0) + 1;
            SIZE_T copy = entry <= room - used ? entry : room - used;
            for (SIZE_T i = 0; i < copy; i++) {
                WCHAR c = i < key ? profile.Text[line.Name + i] : !equals ? 0 : i == key ? L'=' : i < key + 1 + value ? profile.Text[line.Value + i - key - 1] : 0;
                out[used + i] = c;
            }
            used += (DWORD)copy;
            truncated = copy < entry;
        }
    }
    KbFree(profile.Text);
    KbFree(path);
    if (truncated) {
        /* the last error is left alone, only the result says the buffer was too small. */
        if (size < 2)
            return 0;
        out[size - 2] = out[size - 1] = 0;
        return size - 2;
    }
    out[used] = 0;
    if (used + 1 < size)
        out[used + 1] = 0;
    KbSetError(error);
    return used;
}

DWORD WINAPI GetPrivateProfileSectionNamesW(LPWSTR out, DWORD size, LPCWSTR filename)
{
    return GetPrivateProfileStringW(nullptr, nullptr, nullptr, out, size, filename);
}

typedef struct {
    PCWSTR Section, Lines;
    SIZE_T Length;
} SECTION_EDIT;

static BOOL edit_section(const void *arguments, PCWSTR path)
{
    const SECTION_EDIT *edit = arguments;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))) {
        KbSetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    PROFILE profile;
    if (!load_profile(path, &profile, TRUE))
        return FALSE;
    SIZE_T header = profile.Length, body = profile.Length, end = profile.Length;
    BOOL found = FALSE;
    for (SIZE_T at = 0; at < profile.Length;) {
        PROFILE_LINE line = next_line(&profile, at);
        at = line.Next;
        if (!line.Section)
            continue;
        if (found) {
            end = line.Start;
            break;
        }
        if (matches(profile.Text, line.Name, line.NameEnd, edit->Section)) {
            found = TRUE;
            header = line.Start;
            body = line.Next;
        }
    }
    /* deleting what is not there changes nothing, not even whether the file exists. */
    if (!edit->Lines && !found) {
        KbFree(profile.Text);
        KbSetError(ERROR_SUCCESS);
        return TRUE;
    }
    SIZE_T section_length = wcslen(edit->Section);
    SIZE_T capacity = profile.Length + section_length + edit->Length + 16;
    PWSTR output = capacity <= PROFILE_LIMIT ? KbAlloc(capacity * sizeof(WCHAR)) : nullptr;
    if (!output) {
        KbFree(profile.Text);
        KbSetError(capacity > PROFILE_LIMIT ? ERROR_FILE_TOO_LARGE : ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    SIZE_T used = 0;
    if (!edit->Lines) {
        memcpy(output, profile.Text, header * sizeof(WCHAR));
        used = header;
    } else {
        SIZE_T keep = found ? body : profile.Length;
        memcpy(output, profile.Text, keep * sizeof(WCHAR));
        used = keep;
        if (used && output[used - 1] != L'\n' && output[used - 1] != L'\r') {
            output[used++] = L'\r';
            output[used++] = L'\n';
        }
        if (!found) {
            output[used++] = L'[';
            memcpy(output + used, edit->Section, section_length * sizeof(WCHAR));
            used += section_length;
            output[used++] = L']';
            output[used++] = L'\r';
            output[used++] = L'\n';
        }
        for (PCWSTR line = edit->Lines; *line;) {
            SIZE_T length = wcslen(line);
            memcpy(output + used, line, length * sizeof(WCHAR));
            used += length;
            output[used++] = L'\r';
            output[used++] = L'\n';
            line += length + 1;
        }
    }
    if (found) {
        memcpy(output + used, profile.Text + end, (profile.Length - end) * sizeof(WCHAR));
        used += profile.Length - end;
    }
    BOOL result = save_profile(path, output, used, profile.Unicode);
    KbFree(output);
    KbFree(profile.Text);
    return result;
}

/* replaces the section's keys with the list, ended by an empty string, or removes the section. */
BOOL WINAPI WritePrivateProfileSectionW(LPCWSTR section, LPCWSTR lines, LPCWSTR filename)
{
    if (!section || !*section || contains(section, L"\r\n]")) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SIZE_T length = 0;
    if (lines) {
        while (lines[length]) {
            if (length > PROFILE_LIMIT) {
                KbSetError(ERROR_FILE_TOO_LARGE);
                return FALSE;
            }
            length += wcslen(lines + length) + 1;
        }
        length++;
    }
    SECTION_EDIT edit = {section, lines, length};
    return write_locked(filename, edit_section, &edit);
}

DWORD WINAPI GetProfileSectionW(LPCWSTR section, LPWSTR out, DWORD size)
{
    return GetPrivateProfileSectionW(section, out, size, nullptr);
}

BOOL WINAPI WriteProfileSectionW(LPCWSTR section, LPCWSTR lines)
{
    return WritePrivateProfileSectionW(section, lines, nullptr);
}

/* ------------------------------------------------------------------ structures */

/* a structure is stored as two hex digits per byte, followed by the sum of its bytes as one more byte. */
BOOL WINAPI GetPrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID structure, UINT size, LPCWSTR filename)
{
    UINT digits = 2 * size;
    if (digits + 10 < size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = KbAlloc((SIZE_T)(digits + 10) * sizeof(WCHAR));
    if (!text)
        return FALSE;
    BOOL ok = FALSE;
    if (GetPrivateProfileStringW(section, key, nullptr, text, digits + 10, filename) != digits + 2) {
        KbSetError(ERROR_BAD_LENGTH);
    } else {
        BYTE sum = 0, *bytes = structure;
        for (UINT i = 0; i <= size; i++) {
            BYTE high = (BYTE)((text[2 * i] < L'0' || text[2 * i] > L'9' ? text[2 * i] - 7 : text[2 * i]) & 0xf);
            BYTE low = (BYTE)((text[2 * i + 1] < L'0' || text[2 * i + 1] > L'9' ? text[2 * i + 1] - 7 : text[2 * i + 1]) & 0xf);
            BYTE value = (BYTE)(high << 4 | low);
            if (i == size) {
                ok = value == sum;
                if (!ok)
                    KbSetError(ERROR_INVALID_DATA);
                break;
            }
            bytes[i] = value;
            sum += value;
        }
    }
    KbFree(text);
    return ok;
}

BOOL WINAPI WritePrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID structure, UINT size, LPCWSTR filename)
{
    if (!structure)
        return WritePrivateProfileStringW(section, key, nullptr, filename);
    if (2 * size + 3 < size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = KbAlloc((SIZE_T)(2 * size + 3) * sizeof(WCHAR));
    if (!text)
        return FALSE;
    static const WCHAR hex[] = L"0123456789ABCDEF";
    const BYTE *bytes = structure;
    BYTE sum = 0;
    UINT at = 0;
    for (UINT i = 0; i < size; i++) {
        sum += bytes[i];
        text[at++] = hex[bytes[i] >> 4];
        text[at++] = hex[bytes[i] & 15];
    }
    text[at++] = hex[sum >> 4];
    text[at++] = hex[sum & 15];
    text[at] = 0;
    BOOL result = WritePrivateProfileStringW(section, key, text, filename);
    KbFree(text);
    return result;
}

DWORD WINAPI GetProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR out, DWORD size)
{
    return GetPrivateProfileStringW(section, key, fallback, out, size, nullptr);
}

UINT WINAPI GetProfileIntW(LPCWSTR section, LPCWSTR key, INT fallback)
{
    return GetPrivateProfileIntW(section, key, fallback, nullptr);
}

BOOL WINAPI WriteProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value)
{
    return WritePrivateProfileStringW(section, key, value, nullptr);
}
