/* what the files of bcrypt.dll share. */
#ifndef MIME_BCRYPT_PRIVATE_H
#define MIME_BCRYPT_PRIVATE_H

#define MIME_BUILD_BCRYPT
#include <mime/win32/bcrypt.h>
#include <crypto/crypto.h>

#ifndef STATUS_INTEGER_OVERFLOW
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095L)
#endif
/* bcrypt hands out these two hresults for bad rsa blobs. */
#define NTE_BAD_DATA_STATUS ((NTSTATUS)0x80090005L)
#define NTE_NOT_SUPPORTED_STATUS ((NTSTATUS)0x80090029L)

/* the magics of windows' objects, kept so a handle looks the same to code that peeks at it. */
#define ALG_MAGIC 0x55555551
#define KEY_MAGIC 0x55555552
#define HASH_MAGIC 0x55555553
#define SECRET_MAGIC 0x55555554

#define ALG_FLAG_PSEUDO 0x80000000u

/* the chaining modes in the order of the primitive provider. */
enum { MODE_NA, MODE_CBC, MODE_ECB, MODE_CFB, MODE_CCM, MODE_GCM };

enum {
    ID_MD4 = 1, ID_MD5, ID_SHA1, ID_SHA256, ID_SHA384, ID_SHA512, ID_AES_CMAC, ID_AES_GMAC,
    ID_RC4, ID_AES, ID_DES, ID_DESX, ID_3DES, ID_3DES_112,
    ID_RSA, ID_RSA_SIGN, ID_DH, ID_ECDH, ID_ECDH_P256, ID_ECDH_P384, ID_ECDH_P521,
    ID_ECDSA, ID_ECDSA_P256, ID_ECDSA_P384, ID_ECDSA_P521,
    ID_RNG, ID_RNG_FIPS186, ID_RNG_DUAL_EC,
    ID_SP800108, ID_SP80056A, ID_PBKDF2, ID_CAPI_KDF, ID_TLS11_KDF, ID_TLS12_KDF, ID_HKDF,
};

typedef struct ALG_INFO {
    const WCHAR *name;
    ULONG id;
    ULONG iface;
    /* hashes: the primitive and the provider's object lengths, plain and keyed. */
    const MimeHashAlg *hash;
    ULONG object_length;
    ULONG hmac_object_length;
    /* ciphers: block size, key sizes in bytes, the modes allowed as a bit mask and the default. */
    ULONG block;
    ULONG key_min, key_max, key_increment;
    ULONG modes;
    ULONG mode;
    /* the curve of the named curve algorithms. */
    const MimeEcCurve *curve;
} ALG_INFO;

typedef struct ALG {
    ULONG size;
    ULONG magic;
    ULONG flags;
    LONG refs;
    const ALG_INFO *info;
    ULONG mode;
    ULONG message_block;
    const MimeEcCurve *curve;
} ALG;

/* hashes and symmetric keys live in the caller's object buffer, from its first 16 byte boundary. */
typedef struct HASH {
    ULONG size;
    ULONG magic;
    ALG *alg;
    void *heap;
    ULONG reusable;
    ULONG finished;
    ULONG state_size;
    ULONG reserved;
    uint64_t state[];
} HASH;

typedef struct GMAC {
    MimeAes aes;
    MimeGcm gcm;
    UCHAR iv[16];
    ULONG iv_size;
    BOOL started;
    UCHAR tail[16];
    ULONG tail_used;
} GMAC;

enum { KIND_SYMMETRIC = 1, KIND_KDF, KIND_PAIR };

typedef struct KEY {
    ULONG size;
    ULONG magic;
    ALG *alg;
    void *heap;
    ULONG kind;
    ULONG mode;
    ULONG message_block;
    ULONG secret_size;
    UCHAR iv[16];
    UCHAR secret[64];
    union {
        MimeAes aes;
        MimeDes des;
        MimeDes3 des3;
        MimeDesx desx;
        MimeRc4 rc4;
    } u;
} KEY;

/* the head every key shares, so a handle tells its kind before its layout. */
typedef struct KEY_HEADER {
    ULONG size;
    ULONG magic;
    ALG *alg;
    void *heap;
    ULONG kind;
} KEY_HEADER;

/* key derivation keys keep their secret on the heap, up to 2048 bytes. */
typedef struct KDF_KEY {
    ULONG size;
    ULONG magic;
    ALG *alg;
    void *heap;
    ULONG kind;
    ULONG secret_size;
    UCHAR *secret;
    const MimeHashAlg *hkdf_hash;
    ULONG hkdf_ready;
    UCHAR prk[MIME_HASH_MAX_DIGEST];
} KDF_KEY;

/* asymmetric keys are heap objects, as windows allocates them. */
typedef struct KEY_PAIR {
    ULONG size;
    ULONG magic;
    ALG *alg;
    void *heap;
    ULONG kind;
    ULONG bits;
    BOOL finalized;
    BOOL has_private;
    const MimeEcCurve *curve;
    MimeRsa *rsa;
    MimeEcKey *ec;
    MimeDh *dh;
} KEY_PAIR;

typedef struct SECRET {
    ULONG size;
    ULONG magic;
    ALG *alg;
    ULONG length;
    BOOL used;
    UCHAR value[];
} SECRET;

/* main.c */
void *BcAlloc(SIZE_T size);
void *BcAllocZero(SIZE_T size);
void BcFree(void *p);
ALG *BcAlg(BCRYPT_ALG_HANDLE handle);
void BcAlgRef(ALG *alg);
void BcAlgRelease(ALG *alg);
const ALG_INFO *BcFindAlg(LPCWSTR name);
const MimeHashAlg *BcHashByName(LPCWSTR name);
const ALG_INFO *BcInfoById(ULONG id);
NTSTATUS BcStatus(int crypto_status);
NTSTATUS BcOutput(const void *value, ULONG size, PUCHAR output, ULONG capacity, ULONG *result);
ULONG BcObjectLength(const ALG *alg);
NTSTATUS BcChainingMode(ALG *alg, ULONG *mode, ULONG *message_block, PUCHAR input, ULONG size);
NTSTATUS BcCipherCommon(const ALG *alg, ULONG mode, ULONG message_block, LPCWSTR property, PUCHAR output,
                        ULONG capacity, ULONG *result, BOOL *handled);

/* hash.c */
HASH *BcHash(BCRYPT_HASH_HANDLE handle);
NTSTATUS BcHashProperty(HASH *hash, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result);
NTSTATUS BcHashSetProperty(HASH *hash, LPCWSTR property, PUCHAR input, ULONG size);
NTSTATUS BcHashOidList(const ALG_INFO *info, PUCHAR output, ULONG capacity, ULONG *result);

/* cipher.c */
KEY_HEADER *BcKey(BCRYPT_KEY_HANDLE handle);
ULONG BcKeyObjectLength(const ALG_INFO *info, ULONG mode);
NTSTATUS BcKeyProperty(KEY_HEADER *key, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result);
NTSTATUS BcKeySetProperty(KEY_HEADER *key, LPCWSTR property, PUCHAR input, ULONG size);
NTSTATUS BcSymmetricEncrypt(KEY *key, PUCHAR input, ULONG size, void *padding, PUCHAR iv, ULONG iv_size,
                            PUCHAR output, ULONG capacity, ULONG *result, ULONG flags, BOOL encrypt);
NTSTATUS BcDestroySymmetric(KEY *key);

/* asym.c */
NTSTATUS BcPairProperty(KEY_PAIR *key, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result);
NTSTATUS BcPairSetProperty(KEY_PAIR *key, LPCWSTR property, PUCHAR input, ULONG size);
NTSTATUS BcPairEncrypt(KEY_PAIR *key, PUCHAR input, ULONG size, void *padding, PUCHAR output, ULONG capacity,
                       ULONG *result, ULONG flags, BOOL encrypt);
NTSTATUS BcPairExport(KEY_PAIR *key, LPCWSTR type, PUCHAR output, ULONG capacity, ULONG *result);
void BcPairDestroy(KEY_PAIR *key);
NTSTATUS BcEccCurveProperty(const MimeEcCurve *curve, LPCWSTR property, PUCHAR output, ULONG capacity,
                            ULONG *result);
const MimeEcCurve *BcCurveByName(LPCWSTR name);

/* kdf.c */
NTSTATUS BcKdfKeyCreate(ALG *alg, BCRYPT_KEY_HANDLE *handle, PUCHAR object, ULONG object_size, PUCHAR secret,
                        ULONG secret_size);
NTSTATUS BcKdfProperty(KDF_KEY *key, LPCWSTR property, PUCHAR output, ULONG capacity, ULONG *result);
NTSTATUS BcKdfSetProperty(KDF_KEY *key, LPCWSTR property, PUCHAR input, ULONG size);
NTSTATUS BcKdfExport(KDF_KEY *key, LPCWSTR type, PUCHAR output, ULONG capacity, ULONG *result);
void BcKdfDestroy(KDF_KEY *key);
const BCryptBuffer *BcParameter(const BCryptBufferDesc *list, ULONG type);

/* random.c */
BOOL BcRandom(void *buffer, SIZE_T size);

#endif
