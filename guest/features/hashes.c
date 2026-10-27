/* Checks of the files of a feature against the hashes the catalog names. */
#include "featurep.h"
#include "../lib/crypto/crypto.h"

static BOOL hex_equals(const BYTE *digest, SIZE_T bytes, const char *hex)
{
    if (!hex || strlen(hex) != bytes * 2)
        return FALSE;
    for (SIZE_T i = 0; i < bytes; i++) {
        int pair = 0;
        for (int k = 0; k < 2; k++) {
            char c = hex[i * 2 + k];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0)
                return FALSE;
            pair = pair * 16 + v;
        }
        if (digest[i] != pair)
            return FALSE;
    }
    return TRUE;
}

BOOL HashMatches(const BYTE *data, SIZE_T size, const char *sha256, const char *sha1)
{
    if (!sha256 && !sha1)
        return FALSE;
    BYTE digest[32];
    if (sha256) {
        mime_hash(&mime_sha256, data, size, digest);
        if (!hex_equals(digest, 32, sha256))
            return FALSE;
    }
    if (sha1) {
        mime_hash(&mime_sha1, data, size, digest);
        if (!hex_equals(digest, 20, sha1))
            return FALSE;
    }
    return TRUE;
}

BOOL FileMatches(PCWSTR path, const char *sha256)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    MimeHashCtx ctx;
    mime_sha256.init(&ctx);
    BYTE *buffer = HeapAlloc(GetProcessHeap(), 0, 1 << 20);
    BOOL ok = buffer != nullptr;
    DWORD got;
    while (ok && ReadFile(file, buffer, 1 << 20, &got, nullptr) && got)
        mime_sha256.update(&ctx, buffer, got);
    BYTE digest[32];
    mime_sha256.final(&ctx, digest);
    if (buffer)
        HeapFree(GetProcessHeap(), 0, buffer);
    CloseHandle(file);
    return ok && hex_equals(digest, 32, sha256);
}
