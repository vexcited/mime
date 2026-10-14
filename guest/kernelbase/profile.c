#include "kernelbase_private.h"

#define PROFILE_LIMIT (16 * 1024 * 1024)

typedef struct {
    PWSTR Text;
    SIZE_T Length;
    BOOL Unicode;
} PROFILE;

typedef struct {
    SIZE_T Start, Next, Name, NameEnd, Value, ValueEnd;
    BOOL Section, Key;
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
        return NULL;
    }
    SIZE_T length = wcslen(name);
    if (length > 32700) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    PWSTR path = KbAlloc((length + MAX_PATH + 2) * sizeof(WCHAR));
    if (!path) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (name[0] == L'\\' || name[0] == L'/' || (length > 1 && name[1] == L':'))
        wcscpy(path, name);
    else {
        UINT count = GetWindowsDirectoryW(path, MAX_PATH);
        if (!count || count >= MAX_PATH) {
            KbFree(path);
            return NULL;
        }
        path[count++] = L'\\';
        wcscpy(path + count, name);
    }
    return path;
}

static BOOL load_profile(PCWSTR path, PROFILE *profile, BOOL missing_ok)
{
    memset(profile, 0, sizeof(*profile));
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (!missing_ok || GetLastError() != ERROR_FILE_NOT_FOUND)
            return FALSE;
        profile->Text = KbAllocZero(sizeof(WCHAR));
        if (!profile->Text)
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return profile->Text != NULL;
    }
    LARGE_INTEGER size;
    BOOL ok = GetFileSizeEx(file, &size);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (ok && (size.QuadPart < 0 || size.QuadPart > PROFILE_LIMIT)) {
        error = ERROR_FILE_TOO_LARGE;
        ok = FALSE;
    }
    BYTE *bytes = ok ? KbAlloc((SIZE_T)size.QuadPart + 2) : NULL;
    if (ok && !bytes) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        ok = FALSE;
    }
    DWORD count = 0;
    if (ok && (!ReadFile(file, bytes, (DWORD)size.QuadPart, &count, NULL) || count != size.QuadPart)) {
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
                        count ? (SIZE_T)MultiByteToWideChar(CP_ACP, 0, (PCSTR)bytes, count, NULL, 0) : 0;
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
        profile->Text = NULL;
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
    PWSTR path = profile_path(filename);
    PROFILE profile = {0};
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
    int encoded = unicode ? 0 : length ? WideCharToMultiByte(CP_ACP, 0, text, (int)length, NULL, 0, NULL, NULL) : 0;
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
        WideCharToMultiByte(CP_ACP, 0, text, (int)length, (PSTR)bytes, encoded, NULL, NULL);
    HANDLE file = INVALID_HANDLE_VALUE;
    for (ULONG i = 0; i < 32 && file == INVALID_HANDLE_VALUE; i++) {
        KbFormat(temporary, path_size, L"%s.mime-%u-%u.tmp", path, GetCurrentProcessId(),
                 (ULONG)InterlockedIncrement(&temporary_id));
        file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS)
            break;
    }
    BOOL ok = file != INVALID_HANDLE_VALUE;
    DWORD error = ok ? ERROR_SUCCESS : GetLastError(), written;
    if (ok) {
        ok = WriteFile(file, bytes, (DWORD)size, &written, NULL);
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
    PWSTR output = capacity <= PROFILE_LIMIT ? KbAlloc(capacity * sizeof(WCHAR)) : NULL;
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
    PWSTR path = profile_path(filename);
    if (!path)
        return FALSE;
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\MimeIniFiles");
    BOOL result = FALSE;
    if (mutex) {
        DWORD wait = WaitForSingleObject(mutex, INFINITE);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED_0) {
            result = edit_profile(section, key, value, path);
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

DWORD WINAPI GetProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR out, DWORD size)
{
    return GetPrivateProfileStringW(section, key, fallback, out, size, NULL);
}

UINT WINAPI GetProfileIntW(LPCWSTR section, LPCWSTR key, INT fallback)
{
    return GetPrivateProfileIntW(section, key, fallback, NULL);
}

BOOL WINAPI WriteProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value)
{
    return WritePrivateProfileStringW(section, key, value, NULL);
}
