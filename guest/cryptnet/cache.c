#include "cryptnet_private.h"

#define META_MAGIC 0x4355434E
#define META_VERSION 1
#define MAX_CACHED (128u << 20)

typedef struct {
    DWORD magic;
    DWORD version;
    FILETIME synced;
    DWORD size;
    DWORD url_bytes;
} meta_header;

/* the cache of a user lives in its low integrity profile directory, as windows keeps it. */
static BOOL cache_dir(WCHAR *out, DWORD capacity)
{
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", out, capacity);
    if (!n || n >= capacity)
        return FALSE;
    return _snwprintf(out + n, capacity - n, L"\\AppData\\LocalLow\\Microsoft\\CryptnetUrlCache") > 0;
}

/* a url's files are named by the md5 of its utf-16 text, in upper case hexadecimal. */
static BOOL file_name(LPCWSTR url, PCWSTR kind, WCHAR *out, DWORD capacity, BOOL create)
{
    WCHAR dir[MAX_PATH];
    if (!cache_dir(dir, RTL_NUMBER_OF(dir)))
        return FALSE;
    BYTE hash[16];
    DWORD size = sizeof(hash);
    if (!CryptHashCertificate(0, CALG_MD5, 0, (const BYTE *)url, (DWORD)wcslen(url) * sizeof(WCHAR), hash, &size))
        return FALSE;
    if (create) {
        WCHAR path[MAX_PATH];
        _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls\\", dir, kind);
        for (WCHAR *p = path + 3; *p; p++)
            if (*p == '\\') {
                *p = 0;
                CreateDirectoryW(path, nullptr);
                *p = '\\';
            }
    }
    int n = _snwprintf(out, capacity, L"%ls\\%ls\\", dir, kind);
    if (n < 0 || (DWORD)n + 33 > capacity)
        return FALSE;
    static const WCHAR hex[] = L"0123456789ABCDEF";
    for (DWORD i = 0; i < 16; i++) {
        out[n + 2 * i] = hex[hash[i] >> 4];
        out[n + 2 * i + 1] = hex[hash[i] & 15];
    }
    out[n + 32] = 0;
    return TRUE;
}

static BYTE *read_file(LPCWSTR path, DWORD *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    LARGE_INTEGER length;
    BYTE *data = nullptr;
    if (GetFileSizeEx(file, &length) && length.QuadPart && length.QuadPart <= MAX_CACHED &&
        (data = CnAlloc(length.LowPart))) {
        DWORD read = 0;
        if (!ReadFile(file, data, length.LowPart, &read, nullptr) || read != length.LowPart) {
            CnFree(data);
            data = nullptr;
        } else {
            *size = read;
        }
    }
    CloseHandle(file);
    return data;
}

static BOOL write_file(LPCWSTR path, const void *a, DWORD a_size, const void *b, DWORD b_size)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD written;
    BOOL ok = WriteFile(file, a, a_size, &written, nullptr) && written == a_size &&
              (!b_size || (WriteFile(file, b, b_size, &written, nullptr) && written == b_size));
    CloseHandle(file);
    if (!ok)
        DeleteFileW(path);
    return ok;
}

BYTE *CnCacheRead(LPCWSTR url, DWORD *size, FILETIME *synced)
{
    WCHAR meta_path[MAX_PATH], content_path[MAX_PATH];
    if (!file_name(url, L"MetaData", meta_path, MAX_PATH, FALSE) ||
        !file_name(url, L"Content", content_path, MAX_PATH, FALSE))
        return nullptr;
    DWORD meta_size = 0;
    BYTE *meta = read_file(meta_path, &meta_size);
    if (!meta)
        return nullptr;
    const meta_header *h = (const meta_header *)meta;
    DWORD url_bytes = (DWORD)wcslen(url) * sizeof(WCHAR);
    BOOL valid = meta_size >= sizeof(*h) && h->magic == META_MAGIC && h->version == META_VERSION &&
                 h->url_bytes == url_bytes && meta_size - sizeof(*h) >= url_bytes &&
                 !memcmp(meta + sizeof(*h), url, url_bytes);
    DWORD expected = valid ? h->size : 0;
    if (valid && synced)
        *synced = h->synced;
    CnFree(meta);
    if (!valid)
        return nullptr;
    DWORD content_size = 0;
    BYTE *content = read_file(content_path, &content_size);
    if (content && content_size != expected) {
        CnFree(content);
        return nullptr;
    }
    if (content)
        *size = content_size;
    return content;
}

void CnCacheWrite(LPCWSTR url, const BYTE *data, DWORD size)
{
    WCHAR meta_path[MAX_PATH], content_path[MAX_PATH];
    if (!size || !file_name(url, L"MetaData", meta_path, MAX_PATH, TRUE) ||
        !file_name(url, L"Content", content_path, MAX_PATH, TRUE))
        return;
    meta_header h = {META_MAGIC, META_VERSION};
    GetSystemTimeAsFileTime(&h.synced);
    h.size = size;
    h.url_bytes = (DWORD)wcslen(url) * sizeof(WCHAR);
    /* the metadata goes last: a reader sees the old pair or the new one, never new content with old sizes. */
    DeleteFileW(meta_path);
    if (write_file(content_path, data, size, nullptr, 0))
        write_file(meta_path, &h, sizeof(h), url, h.url_bytes);
}

void CnCacheDelete(LPCWSTR url)
{
    WCHAR path[MAX_PATH];
    if (file_name(url, L"MetaData", path, MAX_PATH, FALSE))
        DeleteFileW(path);
    if (file_name(url, L"Content", path, MAX_PATH, FALSE))
        DeleteFileW(path);
}
