/*
 * rsaenh.dll: the entry points of the microsoft enhanced provider. mime's advapi32 implements the microsoft providers
 * itself, so a call made to the provider directly goes to the same implementation through the advapi32 calls.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/wincrypt.h>

typedef struct _VTableProvStruc *PVTableProvStruc;

#define MS_ENH_RSA_AES_PROV_A "Microsoft Enhanced RSA and AES Cryptographic Provider"
#define PROV_RSA_AES 24

/* the calls of advapi32, which no public header here declares. */
DECLSPEC_IMPORT BOOL WINAPI CryptAcquireContextA(HCRYPTPROV *prov, LPCSTR container, LPCSTR provider, DWORD type,
                                                 DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptReleaseContext(HCRYPTPROV prov, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptGenKey(HCRYPTPROV prov, ALG_ID algorithm, DWORD flags, HCRYPTKEY *key);
DECLSPEC_IMPORT BOOL WINAPI CryptDeriveKey(HCRYPTPROV prov, ALG_ID algorithm, HCRYPTHASH hash, DWORD flags,
                                           HCRYPTKEY *key);
DECLSPEC_IMPORT BOOL WINAPI CryptDestroyKey(HCRYPTKEY key);
DECLSPEC_IMPORT BOOL WINAPI CryptSetKeyParam(HCRYPTKEY key, DWORD param, const BYTE *data, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptGetKeyParam(HCRYPTKEY key, DWORD param, BYTE *data, DWORD *length, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptSetProvParam(HCRYPTPROV prov, DWORD param, const BYTE *data, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptGetProvParam(HCRYPTPROV prov, DWORD param, BYTE *data, DWORD *length, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptSetHashParam(HCRYPTHASH hash, DWORD param, const BYTE *data, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptGetHashParam(HCRYPTHASH hash, DWORD param, BYTE *data, DWORD *length, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptExportKey(HCRYPTKEY key, HCRYPTKEY public_key, DWORD type, DWORD flags, BYTE *data,
                                           DWORD *length);
DECLSPEC_IMPORT BOOL WINAPI CryptImportKey(HCRYPTPROV prov, const BYTE *data, DWORD length, HCRYPTKEY public_key,
                                           DWORD flags, HCRYPTKEY *key);
DECLSPEC_IMPORT BOOL WINAPI CryptEncrypt(HCRYPTKEY key, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data,
                                         DWORD *length, DWORD size);
DECLSPEC_IMPORT BOOL WINAPI CryptDecrypt(HCRYPTKEY key, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data,
                                         DWORD *length);
DECLSPEC_IMPORT BOOL WINAPI CryptCreateHash(HCRYPTPROV prov, ALG_ID algorithm, HCRYPTKEY key, DWORD flags,
                                            HCRYPTHASH *hash);
DECLSPEC_IMPORT BOOL WINAPI CryptHashData(HCRYPTHASH hash, const BYTE *data, DWORD length, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptDestroyHash(HCRYPTHASH hash);
DECLSPEC_IMPORT BOOL WINAPI CryptSignHashW(HCRYPTHASH hash, DWORD key_spec, LPCWSTR description, DWORD flags,
                                           BYTE *signature, DWORD *length);
DECLSPEC_IMPORT BOOL WINAPI CryptVerifySignatureW(HCRYPTHASH hash, const BYTE *signature, DWORD length,
                                                  HCRYPTKEY public_key, LPCWSTR description, DWORD flags);
DECLSPEC_IMPORT BOOL WINAPI CryptGenRandom(HCRYPTPROV prov, DWORD length, BYTE *buffer);
DECLSPEC_IMPORT BOOL WINAPI CryptGetUserKey(HCRYPTPROV prov, DWORD key_spec, HCRYPTKEY *key);
DECLSPEC_IMPORT BOOL WINAPI CryptDuplicateKey(HCRYPTKEY key, DWORD *reserved, DWORD flags, HCRYPTKEY *copy);



BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/* the provider of a context made here is the enhanced aes one, whichever name the caller gave. */
BOOL WINAPI CPAcquireContext(HCRYPTPROV *prov, LPSTR container, DWORD flags, PVTableProvStruc table)
{
    return CryptAcquireContextA(prov, container, MS_ENH_RSA_AES_PROV_A, PROV_RSA_AES, flags);
}

BOOL WINAPI CPReleaseContext(HCRYPTPROV prov, DWORD flags)
{
    return CryptReleaseContext(prov, flags);
}

BOOL WINAPI CPGenKey(HCRYPTPROV prov, ALG_ID algorithm, DWORD flags, HCRYPTKEY *key)
{
    return CryptGenKey(prov, algorithm, flags, key);
}

BOOL WINAPI CPDeriveKey(HCRYPTPROV prov, ALG_ID algorithm, HCRYPTHASH hash, DWORD flags, HCRYPTKEY *key)
{
    return CryptDeriveKey(prov, algorithm, hash, flags, key);
}

BOOL WINAPI CPDestroyKey(HCRYPTPROV prov, HCRYPTKEY key)
{
    return CryptDestroyKey(key);
}

BOOL WINAPI CPSetKeyParam(HCRYPTPROV prov, HCRYPTKEY key, DWORD param, const BYTE *data, DWORD flags)
{
    return CryptSetKeyParam(key, param, data, flags);
}

BOOL WINAPI CPGetKeyParam(HCRYPTPROV prov, HCRYPTKEY key, DWORD param, BYTE *data, DWORD *length,
                                        DWORD flags)
{
    return CryptGetKeyParam(key, param, data, length, flags);
}

BOOL WINAPI CPSetProvParam(HCRYPTPROV prov, DWORD param, const BYTE *data, DWORD flags)
{
    return CryptSetProvParam(prov, param, data, flags);
}

BOOL WINAPI CPGetProvParam(HCRYPTPROV prov, DWORD param, BYTE *data, DWORD *length, DWORD flags)
{
    return CryptGetProvParam(prov, param, data, length, flags);
}

BOOL WINAPI CPSetHashParam(HCRYPTPROV prov, HCRYPTHASH hash, DWORD param, const BYTE *data, DWORD flags)
{
    return CryptSetHashParam(hash, param, data, flags);
}

BOOL WINAPI CPGetHashParam(HCRYPTPROV prov, HCRYPTHASH hash, DWORD param, BYTE *data, DWORD *length,
                                         DWORD flags)
{
    return CryptGetHashParam(hash, param, data, length, flags);
}

BOOL WINAPI CPExportKey(HCRYPTPROV prov, HCRYPTKEY key, HCRYPTKEY public_key, DWORD type, DWORD flags,
                                      BYTE *data, DWORD *length)
{
    return CryptExportKey(key, public_key, type, flags, data, length);
}

BOOL WINAPI CPImportKey(HCRYPTPROV prov, const BYTE *data, DWORD length, HCRYPTKEY public_key, DWORD flags,
                                      HCRYPTKEY *key)
{
    return CryptImportKey(prov, data, length, public_key, flags, key);
}

BOOL WINAPI CPEncrypt(HCRYPTPROV prov, HCRYPTKEY key, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data,
                                    DWORD *length, DWORD size)
{
    return CryptEncrypt(key, hash, final, flags, data, length, size);
}

BOOL WINAPI CPDecrypt(HCRYPTPROV prov, HCRYPTKEY key, HCRYPTHASH hash, BOOL final, DWORD flags, BYTE *data,
                                    DWORD *length)
{
    return CryptDecrypt(key, hash, final, flags, data, length);
}

BOOL WINAPI CPCreateHash(HCRYPTPROV prov, ALG_ID algorithm, HCRYPTKEY key, DWORD flags, HCRYPTHASH *hash)
{
    return CryptCreateHash(prov, algorithm, key, flags, hash);
}

BOOL WINAPI CPHashData(HCRYPTPROV prov, HCRYPTHASH hash, const BYTE *data, DWORD length, DWORD flags)
{
    return CryptHashData(hash, data, length, flags);
}

BOOL WINAPI CPDestroyHash(HCRYPTPROV prov, HCRYPTHASH hash)
{
    return CryptDestroyHash(hash);
}

BOOL WINAPI CPSignHash(HCRYPTPROV prov, HCRYPTHASH hash, DWORD key_spec, LPCWSTR description, DWORD flags,
                                     BYTE *signature, DWORD *length)
{
    return CryptSignHashW(hash, key_spec, description, flags, signature, length);
}

BOOL WINAPI CPVerifySignature(HCRYPTPROV prov, HCRYPTHASH hash, const BYTE *signature, DWORD length,
                                            HCRYPTKEY public_key, LPCWSTR description, DWORD flags)
{
    return CryptVerifySignatureW(hash, signature, length, public_key, description, flags);
}

BOOL WINAPI CPGenRandom(HCRYPTPROV prov, DWORD length, BYTE *buffer)
{
    return CryptGenRandom(prov, length, buffer);
}

BOOL WINAPI CPGetUserKey(HCRYPTPROV prov, DWORD key_spec, HCRYPTKEY *key)
{
    return CryptGetUserKey(prov, key_spec, key);
}

BOOL WINAPI CPDuplicateKey(HCRYPTPROV prov, HCRYPTKEY key, DWORD *reserved, DWORD flags, HCRYPTKEY *copy)
{
    return CryptDuplicateKey(key, reserved, flags, copy);
}

/* advapi32 here has no duplicate of a hash nor a hash of a session key. */
BOOL WINAPI CPDuplicateHash(HCRYPTPROV prov, HCRYPTHASH hash, DWORD *reserved, DWORD flags,
                                          HCRYPTHASH *copy)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI CPHashSessionKey(HCRYPTPROV prov, HCRYPTHASH hash, HCRYPTKEY key, DWORD flags)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* the provider is registered with the system already. */
HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}
