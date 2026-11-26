#include "cryptsvc_private.h"
#include "cryptsvc/cryptsvc.h"

WINBASEAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, DWORD klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *text);
WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);
WINADVAPI LSTATUS WINAPI RegSetKeySecurity(HKEY key, DWORD information, PSECURITY_DESCRIPTOR sd);

#define CERT_PROT_INIT_ROOTS 1
#define CERT_PROT_REGISTER_NOTIFICATION 1002
#define TOKEN_QUERY_ 0x0008
#define TOKEN_USER_CLASS 1
#define TOKEN_PRIMARY_GROUP_CLASS 5
#define LABEL_SECURITY_INFORMATION_ 0x10
#define PROTECTED_ROOTS L"Software\\Microsoft\\SystemCertificates\\Root\\ProtectedRoots"
#define USER_ROOTS L"Software\\Microsoft\\SystemCertificates\\Root\\Certificates"
#define CRYPTSVC_SID L"S-1-5-80-242729624-280608522-2219052887-3187409060-2225943459"
#define HASH_SIZE 20
#define MAX_ROOTS 4096

typedef struct PROT_ROOT_HEADER {
    DWORD Size;
    DWORD Version;
    FILETIME LastUpdate;
    DWORD Count;
    DWORD Offset;
} PROT_ROOT_HEADER;

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

/* the string form of a sid of the token the thread impersonates. */
static WCHAR *token_sid(HANDLE token, DWORD klass)
{
    BYTE buffer[256];
    DWORD size = 0;
    WCHAR *text = nullptr;
    if (!GetTokenInformation(token, klass, buffer, sizeof(buffer), &size) || !ConvertSidToStringSidW(*(PSID *)buffer, &text))
        return nullptr;
    return text;
}

static int hex_value(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

/* few roots, put in order as they come. */
static void insert_hash(BYTE *hashes, DWORD count, const BYTE *hash)
{
    DWORD at = count;
    while (at && memcmp(hashes + (SIZE_T)(at - 1) * HASH_SIZE, hash, HASH_SIZE) > 0) {
        memcpy(hashes + (SIZE_T)at * HASH_SIZE, hashes + (SIZE_T)(at - 1) * HASH_SIZE, HASH_SIZE);
        at--;
    }
    memcpy(hashes + (SIZE_T)at * HASH_SIZE, hash, HASH_SIZE);
}

/* the roots of the user's store are named by their sha1 hashes, which the value lists in order. */
static BYTE *user_roots(HKEY user, DWORD *count)
{
    *count = 0;
    HKEY roots;
    if (RegOpenKeyExW(user, USER_ROOTS, 0, KEY_READ, &roots))
        return nullptr;
    BYTE *hashes = nullptr;
    DWORD capacity = 0;
    for (DWORD i = 0; *count < MAX_ROOTS; i++) {
        WCHAR name[64];
        DWORD chars = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(roots, i, name, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        if (chars != 2 * HASH_SIZE)
            continue;
        BYTE hash[HASH_SIZE];
        BOOL valid = TRUE;
        for (DWORD k = 0; k < HASH_SIZE && valid; k++) {
            int high = hex_value(name[2 * k]), low = hex_value(name[2 * k + 1]);
            valid = high >= 0 && low >= 0;
            hash[k] = (BYTE)(high << 4 | low);
        }
        if (!valid)
            continue;
        if (*count == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            BYTE *grown = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)capacity * HASH_SIZE);
            if (!grown)
                break;
            if (hashes) {
                memcpy(grown, hashes, (SIZE_T)*count * HASH_SIZE);
                HeapFree(GetProcessHeap(), 0, hashes);
            }
            hashes = grown;
        }
        insert_hash(hashes, *count, hash);
        ++*count;
    }
    RegCloseKey(roots);
    return hashes;
}

static DWORD write_info(HKEY key, const BYTE *hashes, DWORD count)
{
    DWORD size = sizeof(PROT_ROOT_HEADER) + count * HASH_SIZE;
    BYTE *value = HeapAlloc(GetProcessHeap(), 0, size);
    if (!value)
        return ERROR_NOT_ENOUGH_MEMORY;
    PROT_ROOT_HEADER *head = (PROT_ROOT_HEADER *)value;
    head->Size = sizeof(*head);
    head->Version = 1;
    GetSystemTimeAsFileTime(&head->LastUpdate);
    head->Count = count;
    head->Offset = sizeof(*head);
    if (count)
        memcpy(value + sizeof(*head), hashes, (SIZE_T)count * HASH_SIZE);
    DWORD error = RegSetValueExW(key, L"Certificates", 0, REG_BINARY, value, size);
    HeapFree(GetProcessHeap(), 0, value);
    return error;
}

/*
 * the key ProtectedRoots crypt32 asks for at its first look at the root store of a user. its descriptor and its value
 * are those of windows 11 26300: crypt32 checks that only the service may change the key, that the label is high, and
 * that the value is a header of 24 bytes followed by the sha1 hash of each protected root. the key is made by the
 * caller, whose hive it is, owned by it and read only to it. the label is set by the service itself, as a caller of
 * medium integrity may not label anything high.
 */
static DWORD init_roots(handle_t h)
{
    if (RpcImpersonateClient(h) != RPC_S_OK)
        return ERROR_ACCESS_DENIED;
    HANDLE token = nullptr;
    WCHAR *user = nullptr, *group = nullptr;
    HKEY hive = nullptr, key = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr, label = nullptr;
    DWORD error = ERROR_SUCCESS;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY_, TRUE, &token) || !(user = token_sid(token, TOKEN_USER_CLASS)) ||
        !(group = token_sid(token, TOKEN_PRIMARY_GROUP_CLASS)))
        error = GetLastError() ? GetLastError() : ERROR_INVALID_DATA;
    if (!error) {
        WCHAR text[640];
        _snwprintf(text, RTL_NUMBER_OF(text), L"O:%lsG:%lsD:P(A;CI;KA;;;" CRYPTSVC_SID L")(A;CI;KR;;;%ls)(A;CI;KR;;;S-1-15-3-9)",
                   user, group, user);
        text[RTL_NUMBER_OF(text) - 1] = 0;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text, 1, &sd, nullptr) ||
            !ConvertStringSecurityDescriptorToSecurityDescriptorW(L"S:(ML;;NW;;;HI)", 1, &label, nullptr))
            error = GetLastError();
    }
    if (!error)
        error = RegOpenCurrentUser(KEY_ALL_ACCESS, &hive);
    DWORD count = 0;
    BYTE *hashes = error ? nullptr : user_roots(hive, &count);
    if (!error) {
        SECURITY_ATTRIBUTES attributes = {sizeof(attributes), sd, FALSE};
        error = RegCreateKeyExW(hive, PROTECTED_ROOTS, 0, nullptr, 0, KEY_ALL_ACCESS, &attributes, &key, nullptr);
    }
    if (!error)
        error = write_info(key, hashes, count);
    RpcRevertToSelf();
    if (!error)
        error = RegSetKeySecurity(key, LABEL_SECURITY_INFORMATION_, label);
    if (key)
        RegCloseKey(key);
    if (hive)
        RegCloseKey(hive);
    if (hashes)
        HeapFree(GetProcessHeap(), 0, hashes);
    if (sd)
        LocalFree(sd);
    if (label)
        LocalFree(label);
    if (user)
        LocalFree(user);
    if (group)
        LocalFree(group);
    if (token)
        CloseHandle(token);
    return error;
}

typedef DWORD(WINAPI *SRV_PROTECT)(handle_t h, DWORD id, DWORD flags, LPCWSTR in, BYTE *data, DWORD size,
                                   BYTE **out, DWORD *out_size, void *(WINAPI *alloc)(size_t),
                                   void(WINAPI *free)(void *));

/*
 * like cryptsvc.dll of windows 26300, the functions below 1000 go to crypt32's own server side: the roots of the
 * machine, the protected roots and the auto update of AuthRoot. a root found in the AuthRoot list is added to the
 * machine by function 7, and a service without it makes crypt32 write it to the user's roots, which the next look
 * at that store deletes again, endlessly. the functions above 1000 have no answer: the notification of a change of
 * the roots, as the roots of mime change only by the caller's hand, and the url cache, which is not here.
 */
unsigned long WINAPI keysvc_SSCertProtectFunction_s(handle_t h, unsigned long dwFuncId, unsigned long dwFlags,
                                                    unsigned short *pwszIn, byte *pbIn, unsigned long cbIn,
                                                    byte **ppbOut, unsigned long *pcbOut)
{
    static SRV_PROTECT srv;
    *ppbOut = nullptr;
    *pcbOut = 0;
    if (dwFuncId == CERT_PROT_INIT_ROOTS)
        return init_roots(h);
    if (dwFuncId == CERT_PROT_REGISTER_NOTIFICATION)
        return ERROR_SUCCESS;
    if (dwFuncId >= 1000)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (!srv) {
        HMODULE crypt32 = LoadLibraryW(L"crypt32.dll");
        srv = crypt32 ? (SRV_PROTECT)GetProcAddress(crypt32, "I_CertSrvProtectFunction") : nullptr;
        if (!srv)
            return GetLastError();
    }
    return srv(h, dwFuncId, dwFlags, (LPCWSTR)pwszIn, pbIn, cbIn, ppbOut, (DWORD *)pcbOut, MIDL_user_allocate,
               MIDL_user_free);
}
