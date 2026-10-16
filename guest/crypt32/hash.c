/*
 * the hash algorithms certificates use: md5, sha-1 and the sha-2 family,
 * written from their published specifications.
 */
#include "crypt32_private.h"

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

/* ------------------------------------------------------------------ md5 */

static constexpr DWORD md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static constexpr BYTE md5_r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                               14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               4,  11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void md5_block(DWORD h[4], const BYTE *p)
{
    DWORD m[16];
    for (int i = 0; i < 16; i++)
        m[i] = p[i * 4] | p[i * 4 + 1] << 8 | p[i * 4 + 2] << 16 | (DWORD)p[i * 4 + 3] << 24;
    DWORD a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
        DWORD f, g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        DWORD t = d;
        d = c;
        c = b;
        b = b + ROL32(a + f + md5_k[i] + m[g], md5_r[i]);
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

/* ------------------------------------------------------------------ sha-1 */

static void sha1_block(DWORD h[5], const BYTE *p)
{
    DWORD w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (DWORD)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    DWORD a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        DWORD f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        DWORD t = ROL32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL32(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

/* ------------------------------------------------------------------ sha-256 */

static constexpr DWORD sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_block(DWORD h[8], const BYTE *p)
{
    DWORD w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (DWORD)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        DWORD s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        DWORD s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    DWORD v[8];
    memcpy(v, h, sizeof(v));
    for (int i = 0; i < 64; i++) {
        DWORD s1 = ROR32(v[4], 6) ^ ROR32(v[4], 11) ^ ROR32(v[4], 25);
        DWORD ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        DWORD t1 = v[7] + s1 + ch + sha256_k[i] + w[i];
        DWORD s0 = ROR32(v[0], 2) ^ ROR32(v[0], 13) ^ ROR32(v[0], 22);
        DWORD maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        DWORD t2 = s0 + maj;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++)
        h[i] += v[i];
}

/* ------------------------------------------------------------------ sha-512 and sha-384 */

static const ULONG64 sha512_k[80] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
    0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
    0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
    0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
    0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
    0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

static void sha512_block(ULONG64 h[8], const BYTE *p)
{
    ULONG64 w[80];
    for (int i = 0; i < 16; i++) {
        ULONG64 v = 0;
        for (int j = 0; j < 8; j++)
            v = (v << 8) | p[i * 8 + j];
        w[i] = v;
    }
    for (int i = 16; i < 80; i++) {
        ULONG64 s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        ULONG64 s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    ULONG64 v[8];
    memcpy(v, h, sizeof(v));
    for (int i = 0; i < 80; i++) {
        ULONG64 s1 = ROR64(v[4], 14) ^ ROR64(v[4], 18) ^ ROR64(v[4], 41);
        ULONG64 ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        ULONG64 t1 = v[7] + s1 + ch + sha512_k[i] + w[i];
        ULONG64 s0 = ROR64(v[0], 28) ^ ROR64(v[0], 34) ^ ROR64(v[0], 39);
        ULONG64 maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + s0 + maj;
    }
    for (int i = 0; i < 8; i++)
        h[i] += v[i];
}

/* ------------------------------------------------------------------ driver */

/* runs the merkle damgard padding over a whole message. */
static void run_blocks(void (*block)(void *, const BYTE *), void *state, const BYTE *data, DWORD size,
                       DWORD block_size, BOOL little_endian_length)
{
    DWORD full = size / block_size * block_size;
    for (DWORD i = 0; i < full; i += block_size)
        block(state, data + i);
    BYTE tail[256];
    DWORD rest = size - full;
    memset(tail, 0, sizeof(tail));
    memcpy(tail, data + full, rest);
    tail[rest] = 0x80;
    DWORD length_bytes = block_size == 128 ? 16 : 8;
    DWORD total = rest + 1 + length_bytes <= block_size ? block_size : block_size * 2;
    ULONG64 bits = (ULONG64)size * 8;
    for (DWORD i = 0; i < 8; i++) {
        BYTE b = (BYTE)(bits >> (8 * i));
        if (little_endian_length)
            tail[total - 8 + i] = b;
        else
            tail[total - 1 - i] = b;
    }
    for (DWORD i = 0; i < total; i += block_size)
        block(state, tail + i);
}

static void md5_wrap(void *s, const BYTE *p) { md5_block(s, p); }
static void sha1_wrap(void *s, const BYTE *p) { sha1_block(s, p); }
static void sha256_wrap(void *s, const BYTE *p) { sha256_block(s, p); }
static void sha512_wrap(void *s, const BYTE *p) { sha512_block(s, p); }

DWORD CrHashSize(ALG_ID alg)
{
    switch (alg) {
    case CALG_MD5:
        return 16;
    case CALG_SHA1:
        return 20;
    case CALG_SHA_256:
        return 32;
    case CALG_SHA_384:
        return 48;
    case CALG_SHA_512:
        return 64;
    }
    return 0;
}

BOOL CrHash(ALG_ID alg, const BYTE *data, DWORD size, BYTE *out)
{
    switch (alg) {
    case CALG_MD5: {
        DWORD h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
        run_blocks(md5_wrap, h, data, size, 64, TRUE);
        for (int i = 0; i < 16; i++)
            out[i] = (BYTE)(h[i / 4] >> (8 * (i % 4)));
        return TRUE;
    }
    case CALG_SHA1: {
        DWORD h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        run_blocks(sha1_wrap, h, data, size, 64, FALSE);
        for (int i = 0; i < 20; i++)
            out[i] = (BYTE)(h[i / 4] >> (24 - 8 * (i % 4)));
        return TRUE;
    }
    case CALG_SHA_256: {
        DWORD h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        run_blocks(sha256_wrap, h, data, size, 64, FALSE);
        for (int i = 0; i < 32; i++)
            out[i] = (BYTE)(h[i / 4] >> (24 - 8 * (i % 4)));
        return TRUE;
    }
    case CALG_SHA_384:
    case CALG_SHA_512: {
        static const ULONG64 h384[8] = {0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17,
                                        0x152fecd8f70e5939, 0x67332667ffc00b31, 0x8eb44a8768581511,
                                        0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4};
        static const ULONG64 h512[8] = {0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b,
                                        0xa54ff53a5f1d36f1, 0x510e527fade682d1, 0x9b05688c2b3e6c1f,
                                        0x1f83d9abfb41bd6b, 0x5be0cd19137e2179};
        ULONG64 h[8];
        memcpy(h, alg == CALG_SHA_384 ? h384 : h512, sizeof(h));
        run_blocks(sha512_wrap, h, data, size, 128, FALSE);
        DWORD n = alg == CALG_SHA_384 ? 48 : 64;
        for (DWORD i = 0; i < n; i++)
            out[i] = (BYTE)(h[i / 8] >> (56 - 8 * (i % 8)));
        return TRUE;
    }
    }
    return FALSE;
}

ALG_ID CrHashFromOid(LPCSTR oid)
{
    static const struct {
        LPCSTR oid;
        ALG_ID alg;
    } map[] = {
        {szOID_RSA_MD5, CALG_MD5},       {szOID_OIWSEC_sha1, CALG_SHA1},      {szOID_NIST_sha256, CALG_SHA_256},
        {szOID_NIST_sha384, CALG_SHA_384}, {szOID_NIST_sha512, CALG_SHA_512},
    };
    for (DWORD i = 0; i < RTL_NUMBER_OF(map); i++)
        if (!strcmp(map[i].oid, oid))
            return map[i].alg;
    return 0;
}

/* ------------------------------------------------------------------ exports */

BOOL WINAPI CryptHashCertificate(HCRYPTPROV_LEGACY hCryptProv, ALG_ID Algid, DWORD dwFlags, const BYTE *pbEncoded,
                                 DWORD cbEncoded, BYTE *pbComputedHash, DWORD *pcbComputedHash)
{
    if (!pcbComputedHash)
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!Algid)
        Algid = CALG_SHA1;
    DWORD n = CrHashSize(Algid);
    if (!n)
        return CrFail(NTE_BAD_ALGID);
    if (!pbComputedHash) {
        *pcbComputedHash = n;
        return TRUE;
    }
    if (*pcbComputedHash < n) {
        *pcbComputedHash = n;
        return CrFail(ERROR_MORE_DATA);
    }
    if (!pbEncoded && cbEncoded)
        return CrFail(ERROR_INVALID_PARAMETER);
    CrHash(Algid, pbEncoded, cbEncoded, pbComputedHash);
    *pcbComputedHash = n;
    return TRUE;
}

BOOL WINAPI CryptHashCertificate2(LPCWSTR pwszCNGHashAlgid, DWORD dwFlags, void *pvReserved, const BYTE *pbEncoded,
                                  DWORD cbEncoded, BYTE *pbComputedHash, DWORD *pcbComputedHash)
{
    static const struct {
        LPCWSTR name;
        ALG_ID alg;
    } map[] = {{L"MD5", CALG_MD5},         {L"SHA1", CALG_SHA1},         {L"SHA256", CALG_SHA_256},
               {L"SHA384", CALG_SHA_384}, {L"SHA512", CALG_SHA_512}};
    if (!pwszCNGHashAlgid)
        return CrFail(ERROR_INVALID_PARAMETER);
    for (DWORD i = 0; i < RTL_NUMBER_OF(map); i++)
        if (!_wcsicmp(map[i].name, pwszCNGHashAlgid))
            return CryptHashCertificate(0, map[i].alg, 0, pbEncoded, cbEncoded, pbComputedHash, pcbComputedHash);
    return CrFail(NTE_BAD_ALGID);
}

BOOL WINAPI CryptHashPublicKeyInfo(HCRYPTPROV_LEGACY hCryptProv, ALG_ID Algid, DWORD dwFlags,
                                   DWORD dwCertEncodingType, PCERT_PUBLIC_KEY_INFO pInfo, BYTE *pbComputedHash,
                                   DWORD *pcbComputedHash)
{
    if (!pInfo)
        return CrFail(ERROR_INVALID_PARAMETER);
    DWORD size;
    BYTE *encoded = CrEncodeAlloc(X509_PUBLIC_KEY_INFO, pInfo, 0, &size);
    if (!encoded)
        return FALSE;
    BOOL ok = CryptHashCertificate(hCryptProv, Algid ? Algid : CALG_MD5, dwFlags, encoded, size, pbComputedHash,
                                   pcbComputedHash);
    CrFree(encoded);
    return ok;
}

BOOL WINAPI CryptHashToBeSigned(HCRYPTPROV_LEGACY hCryptProv, DWORD dwCertEncodingType, const BYTE *pbEncoded,
                                DWORD cbEncoded, BYTE *pbComputedHash, DWORD *pcbComputedHash)
{
    DWORD size;
    CERT_SIGNED_CONTENT_INFO *info = CrDecodeAlloc(X509_CERT, pbEncoded, cbEncoded, 0, &size);
    if (!info)
        return FALSE;
    BOOL ok = CryptHashCertificate(hCryptProv, CALG_MD5, 0, info->ToBeSigned.pbData, info->ToBeSigned.cbData,
                                   pbComputedHash, pcbComputedHash);
    CrFree(info);
    return ok;
}
