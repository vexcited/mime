/*
 * the tls client: tls 1.3 of rfc 8446, and tls 1.2 of rfc 5246 with the
 * ecdhe and aes gcm suites of rfc 5288 and rfc 8422, the extended master
 * secret of rfc 7627 and the renegotiation indication of rfc 5746. the
 * ClientHello names the server with rfc 6066 and offers protocols with
 * rfc 7301.
 *
 * the engine reads whole records from the caller's buffers, decrypting
 * them in place, and queues the records it sends in an output buffer that
 * InitializeSecurityContext hands over as its token.
 */
#include "schannel.h"

enum {
    REC_CCS = 20,
    REC_ALERT = 21,
    REC_HANDSHAKE = 22,
    REC_DATA = 23,
};

enum {
    HS_HELLO_REQUEST = 0,
    HS_CLIENT_HELLO = 1,
    HS_SERVER_HELLO = 2,
    HS_NEW_SESSION_TICKET = 4,
    HS_ENCRYPTED_EXTENSIONS = 8,
    HS_CERTIFICATE = 11,
    HS_SERVER_KEY_EXCHANGE = 12,
    HS_CERTIFICATE_REQUEST = 13,
    HS_SERVER_HELLO_DONE = 14,
    HS_CERTIFICATE_VERIFY = 15,
    HS_CLIENT_KEY_EXCHANGE = 16,
    HS_FINISHED = 20,
    HS_KEY_UPDATE = 24,
    HS_MESSAGE_HASH = 254,
};

enum {
    EXT_SERVER_NAME = 0,
    EXT_SUPPORTED_GROUPS = 10,
    EXT_EC_POINT_FORMATS = 11,
    EXT_SIGNATURE_ALGORITHMS = 13,
    EXT_ALPN = 16,
    EXT_EXTENDED_MASTER_SECRET = 23,
    EXT_SUPPORTED_VERSIONS = 43,
    EXT_COOKIE = 44,
    EXT_PSK_MODES = 45,
    EXT_KEY_SHARE = 51,
    EXT_RENEGOTIATION_INFO = 0xFF01,
};

enum {
    GROUP_P256 = 23,
    GROUP_P384 = 24,
    GROUP_X25519 = 29,
};

enum {
    ALERT_CLOSE_NOTIFY = 0,
    ALERT_UNEXPECTED_MESSAGE = 10,
    ALERT_BAD_RECORD_MAC = 20,
    ALERT_RECORD_OVERFLOW = 22,
    ALERT_HANDSHAKE_FAILURE = 40,
    ALERT_BAD_CERTIFICATE = 42,
    ALERT_UNSUPPORTED_CERTIFICATE = 43,
    ALERT_ILLEGAL_PARAMETER = 47,
    ALERT_DECODE_ERROR = 50,
    ALERT_DECRYPT_ERROR = 51,
    ALERT_PROTOCOL_VERSION = 70,
    ALERT_INTERNAL_ERROR = 80,
    ALERT_MISSING_EXTENSION = 109,
    ALERT_UNSUPPORTED_EXTENSION = 110,
    ALERT_NO_APPLICATION_PROTOCOL = 120,
};

enum {
    ST_START,
    ST_WAIT_SERVER_HELLO,
    ST13_WAIT_EXTENSIONS,
    ST13_WAIT_CERTIFICATE_OR_REQUEST,
    ST13_WAIT_CERTIFICATE,
    ST13_WAIT_VERIFY,
    ST13_WAIT_FINISHED,
    ST12_WAIT_CERTIFICATE,
    ST12_WAIT_KEY_EXCHANGE,
    ST12_WAIT_DONE,
    ST12_WAIT_CCS,
    ST12_WAIT_FINISHED,
    ST_CONNECTED,
    ST_FAILED,
};

#define TLS_MAX_PLAIN 16384
#define TLS_MAX_MESSAGE (256 * 1024)

/* sha-256 of "HelloRetryRequest", the random of a HelloRetryRequest. */
static const BYTE hello_retry_random[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                            0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                            0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};
/* the end of a tls 1.2 server random when the server could have done tls 1.3. */
static const BYTE downgrade_12[8] = {0x44, 0x4F, 0x57, 0x4E, 0x47, 0x52, 0x44, 0x01};

typedef struct TLS_SUITE {
    USHORT Id;
    USHORT Version;
    BYTE KeySize;
    /* tls 1.2 only: the certificate signs with ecdsa rather than rsa. */
    bool Ecdsa;
    const MimeHashAlg *Hash;
    PCWSTR Name;
} TLS_SUITE;

static const TLS_SUITE suites[] = {
    {0x1301, 0x0304, 16, false, &mime_sha256, L"TLS_AES_128_GCM_SHA256"},
    {0x1302, 0x0304, 32, false, &mime_sha384, L"TLS_AES_256_GCM_SHA384"},
    {0xC02B, 0x0303, 16, true, &mime_sha256, L"TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"},
    {0xC02C, 0x0303, 32, true, &mime_sha384, L"TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"},
    {0xC02F, 0x0303, 16, false, &mime_sha256, L"TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"},
    {0xC030, 0x0303, 32, false, &mime_sha384, L"TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"},
};

/* the signature schemes offered, best first. */
static const USHORT schemes[] = {0x0403, 0x0503, 0x0603, 0x0804, 0x0805, 0x0806, 0x0401, 0x0501, 0x0601};

static const USHORT groups[] = {GROUP_X25519, GROUP_P256, GROUP_P384};

typedef struct TLS_CIPHER {
    MimeAes Aes;
    /* tls 1.3 xors the sequence into the whole iv, tls 1.2 takes the first 4 bytes as salt. */
    BYTE Iv[12];
    UINT64 Seq;
    bool Active;
} TLS_CIPHER;

struct TLS {
    SCH_CONFIG Config;
    ULONG Requested;
    PWSTR Target;
    char Host[256];
    BYTE Alpn[256];
    SIZE_T AlpnSize;
    int State;
    SECURITY_STATUS Error;

    BYTE ClientRandom[32];
    BYTE ServerRandom[32];
    BYTE SessionId[32];
    BYTE SessionIdSize;
    BYTE ServerSessionId[32];
    BYTE ServerSessionIdSize;
    USHORT Version;
    const TLS_SUITE *Suite;
    USHORT Group;
    bool Retried;
    TLS_BUF Cookie;

    /* the key shares of the ClientHello, or of the tls 1.2 key exchange. */
    BYTE X25519[32];
    MimeEcKey P256;
    MimeEcKey P384;
    bool HaveX25519, HaveP256, HaveP384;

    TLS_BUF Transcript;
    TLS_BUF HandshakeIn;
    TLS_BUF Out;
    TLS_CIPHER Read;
    TLS_CIPHER Write;
    /* tls 1.2 keeps the server's keys until its ChangeCipherSpec. */
    TLS_CIPHER PendingRead;

    BYTE Master[48];
    BYTE ClientTraffic[48];
    BYTE ServerTraffic[48];
    BYTE ClientHandshake[48];
    BYTE ServerHandshake[48];
    bool Ems;

    TLS_CERTS Certs;
    TLS_PEER_KEY Key;
    bool CertRequested;
    BYTE RequestContext[255];
    BYTE RequestContextSize;
    bool CcsSent;
    BYTE Protocol[255];
    BYTE ProtocolSize;
    USHORT PeerGroup;

    /* the post handshake record DecryptMessage left for InitializeSecurityContext. */
    SIZE_T PendingSkip;
    bool KeyUpdatePending;
    bool AlertPending;
    BYTE AlertLevel, AlertNumber;
    bool ReceivedClose, SentClose;
    BYTE Warnings;
};

/* ------------------------------------------------------------------ buffers */

bool TlsBufPut(TLS_BUF *b, const void *data, SIZE_T size)
{
    if (b->Failed)
        return false;
    if (size > b->Capacity - b->Size) {
        SIZE_T capacity = b->Capacity ? b->Capacity : 256;
        while (capacity - b->Size < size)
            capacity *= 2;
        BYTE *p = SecHeapAlloc(capacity);
        if (!p) {
            b->Failed = true;
            return false;
        }
        if (b->Size)
            memcpy(p, b->Data, b->Size);
        if (b->Data) {
            mime_wipe(b->Data, b->Capacity);
            SecHeapFree(b->Data);
        }
        b->Data = p;
        b->Capacity = capacity;
    }
    if (size)
        memcpy(b->Data + b->Size, data, size);
    b->Size += size;
    return true;
}

void TlsBufFree(TLS_BUF *b)
{
    if (b->Data) {
        mime_wipe(b->Data, b->Capacity);
        SecHeapFree(b->Data);
    }
    memset(b, 0, sizeof(*b));
}

static void put8(TLS_BUF *b, unsigned v)
{
    BYTE x = (BYTE)v;
    TlsBufPut(b, &x, 1);
}

static void put16(TLS_BUF *b, unsigned v)
{
    BYTE x[2] = {(BYTE)(v >> 8), (BYTE)v};
    TlsBufPut(b, x, 2);
}

/* a vector of width length bytes: open writes the placeholder, close fills it in. */
static SIZE_T open_vector(TLS_BUF *b, int width)
{
    static const BYTE zero[3] = {};
    SIZE_T at = b->Size;
    TlsBufPut(b, zero, width);
    return at;
}

static void close_vector(TLS_BUF *b, SIZE_T at, int width)
{
    if (b->Failed)
        return;
    SIZE_T n = b->Size - at - width;
    for (int i = 0; i < width; i++)
        b->Data[at + i] = (BYTE)(n >> (8 * (width - 1 - i)));
}

static void drop_front(TLS_BUF *b, SIZE_T n)
{
    memmove(b->Data, b->Data + n, b->Size - n);
    b->Size -= n;
}

typedef struct READER {
    const BYTE *P;
    SIZE_T Left;
} READER;

static bool take(READER *r, SIZE_T n, const BYTE **out)
{
    if (r->Left < n)
        return false;
    if (out)
        *out = r->P;
    r->P += n;
    r->Left -= n;
    return true;
}

static bool read_int(READER *r, int width, unsigned *v)
{
    const BYTE *p;
    if (!take(r, width, &p))
        return false;
    *v = 0;
    for (int i = 0; i < width; i++)
        *v = *v << 8 | p[i];
    return true;
}

static bool read_vector(READER *r, int width, READER *out)
{
    unsigned n;
    if (!read_int(r, width, &n))
        return false;
    out->P = r->P;
    out->Left = n;
    return take(r, n, nullptr);
}

static unsigned get16(const BYTE *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

/* ------------------------------------------------------------------ alerts and failure */

/* what a received alert means to the caller, schannel's TlsParseAlertMessage. */
static SECURITY_STATUS alert_status(BYTE level, BYTE number)
{
    if (level == TLS1_ALERT_WARNING) {
        if (number == ALERT_CLOSE_NOTIFY)
            return SEC_I_CONTEXT_EXPIRED;
        if (number == 100)
            return SEC_I_NO_RENEGOTIATION;
        return SEC_E_OK;
    }
    switch (number) {
    case 20:
    case 30:
        return SEC_E_MESSAGE_ALTERED;
    case 21:
    case 51:
        return SEC_E_DECRYPT_FAILURE;
    case 42:
    case 43:
    case 46:
        return SEC_E_CERT_UNKNOWN;
    case 44:
        return CRYPT_E_REVOKED;
    case 45:
        return SEC_E_CERT_EXPIRED;
    case 48:
        return SEC_E_UNTRUSTED_ROOT;
    case 49:
        return SEC_E_LOGON_DENIED;
    case 70:
        return SEC_E_UNSUPPORTED_FUNCTION;
    case 71:
        return SEC_E_ALGORITHM_MISMATCH;
    case 80:
        return SEC_E_INTERNAL_ERROR;
    case 120:
        return SEC_E_APPLICATION_PROTOCOL_MISMATCH;
    default:
        return SEC_E_ILLEGAL_MESSAGE;
    }
}

static void write_record(TLS *t, BYTE type, const BYTE *data, SIZE_T size);

/* sends a fatal alert and makes the failure stick; status 0 takes the alert's own meaning. */
static SECURITY_STATUS fail(TLS *t, BYTE alert, SECURITY_STATUS status)
{
    if (t->State == ST_FAILED)
        return t->Error;
    if (!status)
        status = alert_status(TLS1_ALERT_FATAL, alert);
    if (t->Requested & ISC_REQ_EXTENDED_ERROR) {
        BYTE body[2] = {TLS1_ALERT_FATAL, alert};
        write_record(t, REC_ALERT, body, 2);
    }
    t->State = ST_FAILED;
    t->Error = status;
    return status;
}

static SECURITY_STATUS no_memory(TLS *t)
{
    return fail(t, ALERT_INTERNAL_ERROR, SEC_E_INSUFFICIENT_MEMORY);
}

/* ------------------------------------------------------------------ record protection */

static void cipher_nonce(const TLS_CIPHER *c, bool tls13, BYTE *nonce)
{
    memcpy(nonce, c->Iv, 12);
    if (tls13) {
        for (int i = 0; i < 8; i++)
            nonce[4 + i] ^= (BYTE)(c->Seq >> (56 - 8 * i));
    } else {
        for (int i = 0; i < 8; i++)
            nonce[4 + i] = (BYTE)(c->Seq >> (56 - 8 * i));
    }
}

static bool is13(const TLS *t)
{
    return t->Version == 0x0304;
}

/* the bytes a protected record adds before and after its content. */
static SIZE_T seal_header_size(const TLS *t)
{
    return is13(t) ? 5 : 13;
}

static SIZE_T seal_trailer_size(const TLS *t)
{
    return is13(t) ? 17 : 16;
}

/* protects one record: header gets the record header, data is encrypted in place, trailer gets the rest. */
static void seal(TLS *t, BYTE type, BYTE *header, BYTE *data, SIZE_T size, BYTE *trailer)
{
    TLS_CIPHER *c = &t->Write;
    BYTE nonce[12], aad[13];
    MimeGcm g;
    cipher_nonce(c, is13(t), nonce);
    if (is13(t)) {
        SIZE_T length = size + 17;
        header[0] = REC_DATA;
        header[1] = 3;
        header[2] = 3;
        header[3] = (BYTE)(length >> 8);
        header[4] = (BYTE)length;
        mime_gcm_init(&g, &c->Aes, nonce, 12);
        mime_gcm_aad(&g, header, 5);
        /* the content type follows the data, so the last partial block goes through a copy. */
        SIZE_T whole = size & ~(SIZE_T)15, rest = size - whole;
        BYTE tail[16];
        mime_gcm_encrypt(&g, &c->Aes, data, data, whole);
        memcpy(tail, data + whole, rest);
        tail[rest] = type;
        mime_gcm_encrypt(&g, &c->Aes, tail, tail, rest + 1);
        memcpy(data + whole, tail, rest);
        trailer[0] = tail[rest];
        mime_gcm_tag(&g, &c->Aes, trailer + 1, 16);
    } else {
        SIZE_T length = size + 24;
        header[0] = type;
        header[1] = 3;
        header[2] = 3;
        header[3] = (BYTE)(length >> 8);
        header[4] = (BYTE)length;
        memcpy(header + 5, nonce + 4, 8);
        memcpy(aad, nonce + 4, 8);
        aad[8] = type;
        aad[9] = 3;
        aad[10] = 3;
        aad[11] = (BYTE)(size >> 8);
        aad[12] = (BYTE)size;
        mime_gcm_init(&g, &c->Aes, nonce, 12);
        mime_gcm_aad(&g, aad, 13);
        mime_gcm_encrypt(&g, &c->Aes, data, data, size);
        mime_gcm_tag(&g, &c->Aes, trailer, 16);
    }
    c->Seq++;
    mime_wipe(&g, sizeof(g));
}

/*
 * opens one protected record of total bytes in place. plain points at the
 * content and type gets the content type, the inner one for tls 1.3.
 */
static SECURITY_STATUS open_record(TLS *t, BYTE *record, SIZE_T total, BYTE **plain, SIZE_T *size, BYTE *type,
                                   BYTE *alert)
{
    TLS_CIPHER *c = &t->Read;
    BYTE nonce[12], aad[13], tag[16];
    MimeGcm g;
    SIZE_T length = total - 5;
    if (is13(t)) {
        if (record[0] != REC_DATA || length < 17 || length > TLS_MAX_PLAIN + 256) {
            *alert = length > TLS_MAX_PLAIN + 256 ? ALERT_RECORD_OVERFLOW : ALERT_UNEXPECTED_MESSAGE;
            return SEC_E_ILLEGAL_MESSAGE;
        }
        SIZE_T n = length - 16;
        cipher_nonce(c, true, nonce);
        mime_gcm_init(&g, &c->Aes, nonce, 12);
        mime_gcm_aad(&g, record, 5);
        mime_gcm_decrypt(&g, &c->Aes, record + 5, record + 5, n);
        mime_gcm_tag(&g, &c->Aes, tag, 16);
        if (!mime_ct_equal(tag, record + 5 + n, 16)) {
            *alert = ALERT_BAD_RECORD_MAC;
            return SEC_E_DECRYPT_FAILURE;
        }
        c->Seq++;
        while (n && !record[5 + n - 1])
            n--;
        if (!n) {
            *alert = ALERT_UNEXPECTED_MESSAGE;
            return SEC_E_INVALID_TOKEN;
        }
        *type = record[5 + n - 1];
        *plain = record + 5;
        *size = n - 1;
        if (*size > TLS_MAX_PLAIN) {
            *alert = ALERT_RECORD_OVERFLOW;
            return SEC_E_ILLEGAL_MESSAGE;
        }
    } else {
        if (length < 24 || length > TLS_MAX_PLAIN + 2048) {
            *alert = length < 24 ? ALERT_BAD_RECORD_MAC : ALERT_RECORD_OVERFLOW;
            return length < 24 ? SEC_E_DECRYPT_FAILURE : SEC_E_ILLEGAL_MESSAGE;
        }
        SIZE_T n = length - 24;
        memcpy(nonce, c->Iv, 4);
        memcpy(nonce + 4, record + 5, 8);
        for (int i = 0; i < 8; i++)
            aad[i] = (BYTE)(c->Seq >> (56 - 8 * i));
        aad[8] = record[0];
        aad[9] = record[1];
        aad[10] = record[2];
        aad[11] = (BYTE)(n >> 8);
        aad[12] = (BYTE)n;
        mime_gcm_init(&g, &c->Aes, nonce, 12);
        mime_gcm_aad(&g, aad, 13);
        mime_gcm_decrypt(&g, &c->Aes, record + 13, record + 13, n);
        mime_gcm_tag(&g, &c->Aes, tag, 16);
        if (!mime_ct_equal(tag, record + 13 + n, 16)) {
            *alert = ALERT_BAD_RECORD_MAC;
            return SEC_E_DECRYPT_FAILURE;
        }
        c->Seq++;
        if (n > TLS_MAX_PLAIN) {
            *alert = ALERT_RECORD_OVERFLOW;
            return SEC_E_ILLEGAL_MESSAGE;
        }
        *type = record[0];
        *plain = record + 13;
        *size = n;
    }
    mime_wipe(&g, sizeof(g));
    return SEC_E_OK;
}

/* queues records of type carrying data, protected once the write keys are set. */
static void write_record(TLS *t, BYTE type, const BYTE *data, SIZE_T size)
{
    do {
        SIZE_T n = size > TLS_MAX_PLAIN ? TLS_MAX_PLAIN : size;
        if (!t->Write.Active) {
            /* the first ClientHello goes out as tls 1.0, as most clients do for old middleboxes. */
            BYTE header[5] = {type, 3, (BYTE)(t->Version ? 3 : 1), (BYTE)(n >> 8), (BYTE)n};
            TlsBufPut(&t->Out, header, 5);
            TlsBufPut(&t->Out, data, n);
        } else {
            SIZE_T head = seal_header_size(t), tail = seal_trailer_size(t), at = t->Out.Size;
            BYTE room[13 + 17] = {};
            if (!TlsBufPut(&t->Out, room, head) || !TlsBufPut(&t->Out, data, n) || !TlsBufPut(&t->Out, room, tail))
                return;
            BYTE *p = t->Out.Data + at;
            seal(t, type, p, p + head, n, p + head + n);
        }
        data += n;
        size -= n;
    } while (size);
}

/* ------------------------------------------------------------------ key schedule */

static UINT hash_size(const TLS *t)
{
    return t->Suite->Hash->digest_size;
}

static void transcript_hash(const TLS *t, BYTE *out)
{
    mime_hash(t->Suite->Hash, t->Transcript.Data, t->Transcript.Size, out);
}

static void add_transcript(TLS *t, const BYTE *message, SIZE_T size)
{
    TlsBufPut(&t->Transcript, message, size);
}

/* HKDF-Expand-Label of rfc 8446 7.1. */
static void expand_label(const MimeHashAlg *h, const BYTE *secret, const char *label, const BYTE *context,
                         SIZE_T context_size, BYTE *out, SIZE_T size)
{
    BYTE info[2 + 1 + 255 + 1 + 64];
    SIZE_T n = strlen(label);
    info[0] = (BYTE)(size >> 8);
    info[1] = (BYTE)size;
    info[2] = (BYTE)(6 + n);
    memcpy(info + 3, "tls13 ", 6);
    memcpy(info + 9, label, n);
    info[9 + n] = (BYTE)context_size;
    if (context_size)
        memcpy(info + 10 + n, context, context_size);
    mime_hkdf_expand(h, secret, h->digest_size, info, 10 + n + context_size, out, size);
}

static void set_keys13(TLS *t, TLS_CIPHER *c, const BYTE *secret)
{
    BYTE key[32];
    expand_label(t->Suite->Hash, secret, "key", nullptr, 0, key, t->Suite->KeySize);
    expand_label(t->Suite->Hash, secret, "iv", nullptr, 0, c->Iv, 12);
    mime_aes_init(&c->Aes, key, t->Suite->KeySize);
    c->Seq = 0;
    c->Active = true;
    mime_wipe(key, sizeof(key));
}

/* the handshake secrets of rfc 8446 7.1 once the ServerHello is in the transcript. */
static void derive_handshake13(TLS *t, const BYTE *shared, SIZE_T shared_size)
{
    const MimeHashAlg *h = t->Suite->Hash;
    UINT hl = hash_size(t);
    BYTE zeros[48] = {}, early[48], empty[48], derived[48], secret[48], th[48];
    mime_hkdf_extract(h, zeros, hl, zeros, hl, early);
    mime_hash(h, nullptr, 0, empty);
    expand_label(h, early, "derived", empty, hl, derived, hl);
    mime_hkdf_extract(h, derived, hl, shared, shared_size, secret);
    transcript_hash(t, th);
    expand_label(h, secret, "c hs traffic", th, hl, t->ClientHandshake, hl);
    expand_label(h, secret, "s hs traffic", th, hl, t->ServerHandshake, hl);
    expand_label(h, secret, "derived", empty, hl, derived, hl);
    mime_hkdf_extract(h, derived, hl, zeros, hl, t->Master);
    set_keys13(t, &t->Read, t->ServerHandshake);
    mime_wipe(early, sizeof(early));
    mime_wipe(secret, sizeof(secret));
    mime_wipe(derived, sizeof(derived));
}

static void finished_mac(TLS *t, const BYTE *base, const BYTE *th, BYTE *out)
{
    BYTE key[48];
    UINT hl = hash_size(t);
    expand_label(t->Suite->Hash, base, "finished", nullptr, 0, key, hl);
    mime_hmac(t->Suite->Hash, key, hl, th, hl, out);
    mime_wipe(key, sizeof(key));
}

static void prf12(TLS *t, const BYTE *secret, SIZE_T secret_size, const char *label, const BYTE *seed,
                  SIZE_T seed_size, BYTE *out, SIZE_T size)
{
    mime_tls_prf(t->Suite->Hash, secret, secret_size, label, strlen(label), seed, seed_size, out, size);
}

/* ------------------------------------------------------------------ key shares */

static SECURITY_STATUS make_share(TLS *t, USHORT group)
{
    int status = MIME_CRYPTO_OK;
    if (group == GROUP_X25519 && !t->HaveX25519) {
        if (!SchRandom(t->X25519, 32))
            return SEC_E_INTERNAL_ERROR;
        t->HaveX25519 = true;
    } else if (group == GROUP_P256 && !t->HaveP256) {
        status = mime_ec_generate(&t->P256, &mime_p256);
        t->HaveP256 = !status;
    } else if (group == GROUP_P384 && !t->HaveP384) {
        status = mime_ec_generate(&t->P384, &mime_p384);
        t->HaveP384 = !status;
    }
    return status ? SEC_E_INTERNAL_ERROR : SEC_E_OK;
}

/* our public value for group, in the encoding of key_share and ClientKeyExchange. */
static void put_share(TLS *t, TLS_BUF *b, USHORT group)
{
    if (group == GROUP_X25519) {
        BYTE pub[32];
        mime_x25519_base(pub, t->X25519);
        TlsBufPut(b, pub, 32);
    } else {
        const MimeEcKey *k = group == GROUP_P256 ? &t->P256 : &t->P384;
        UINT size = mime_ec_size(k->curve);
        put8(b, 4);
        TlsBufPut(b, k->x, size);
        TlsBufPut(b, k->y, size);
    }
}

/* the shared secret with the peer's value for group; false when the value is not a valid point. */
static bool agree(TLS *t, USHORT group, const BYTE *peer, SIZE_T size, BYTE *secret, SIZE_T *secret_size)
{
    if (group == GROUP_X25519) {
        if (size != 32 || mime_x25519(secret, t->X25519, peer))
            return false;
        *secret_size = 32;
        return true;
    }
    const MimeEcKey *mine = group == GROUP_P256 ? &t->P256 : &t->P384;
    UINT n = mime_ec_size(mine->curve);
    if (size != 1 + 2 * n || peer[0] != 4)
        return false;
    MimeEcKey other = {.curve = mine->curve};
    memcpy(other.x, peer + 1, n);
    memcpy(other.y, peer + 1 + n, n);
    if (mime_ecdh(mine, &other, secret))
        return false;
    *secret_size = n;
    return true;
}

static bool group_offered(USHORT group)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(groups); i++)
        if (groups[i] == group)
            return true;
    return false;
}

/* ------------------------------------------------------------------ the ClientHello */

static bool suite_allowed(const TLS *t, const TLS_SUITE *s)
{
    ULONG protocol = s->Version == 0x0304 ? SP_PROT_TLS1_3_CLIENT : SP_PROT_TLS1_2_CLIENT;
    ULONG bits = s->KeySize * 8;
    if (!(t->Config.Protocols & protocol))
        return false;
    if (t->Config.MinStrength && bits < t->Config.MinStrength)
        return false;
    if (t->Config.MaxStrength && bits > t->Config.MaxStrength)
        return false;
    return true;
}

static const TLS_SUITE *find_suite(const TLS *t, unsigned id)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(suites); i++)
        if (suites[i].Id == id && suite_allowed(t, &suites[i]))
            return &suites[i];
    return nullptr;
}

static void put_extension_start(TLS_BUF *b, unsigned type, SIZE_T *at)
{
    put16(b, type);
    *at = open_vector(b, 2);
}

static void build_client_hello(TLS *t, TLS_BUF *m)
{
    bool v13 = t->Config.Protocols & SP_PROT_TLS1_3_CLIENT, v12 = t->Config.Protocols & SP_PROT_TLS1_2_CLIENT;
    SIZE_T body, list, ext, at, inner;
    put8(m, HS_CLIENT_HELLO);
    body = open_vector(m, 3);
    put16(m, 0x0303);
    TlsBufPut(m, t->ClientRandom, 32);
    put8(m, t->SessionIdSize);
    TlsBufPut(m, t->SessionId, t->SessionIdSize);
    list = open_vector(m, 2);
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(suites); i++)
        if (suite_allowed(t, &suites[i]))
            put16(m, suites[i].Id);
    close_vector(m, list, 2);
    put8(m, 1);
    put8(m, 0);

    ext = open_vector(m, 2);
    if (t->Host[0]) {
        SIZE_T n = strlen(t->Host);
        put_extension_start(m, EXT_SERVER_NAME, &at);
        put16(m, n + 3);
        put8(m, 0);
        put16(m, n);
        TlsBufPut(m, t->Host, n);
        close_vector(m, at, 2);
    }
    put_extension_start(m, EXT_SUPPORTED_GROUPS, &at);
    put16(m, 2 * RTL_NUMBER_OF(groups));
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(groups); i++)
        put16(m, groups[i]);
    close_vector(m, at, 2);
    if (v12) {
        put_extension_start(m, EXT_EC_POINT_FORMATS, &at);
        put8(m, 1);
        put8(m, 0);
        close_vector(m, at, 2);
    }
    put_extension_start(m, EXT_SIGNATURE_ALGORITHMS, &at);
    put16(m, 2 * RTL_NUMBER_OF(schemes));
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(schemes); i++)
        put16(m, schemes[i]);
    close_vector(m, at, 2);
    if (t->AlpnSize) {
        put_extension_start(m, EXT_ALPN, &at);
        put16(m, t->AlpnSize);
        TlsBufPut(m, t->Alpn, t->AlpnSize);
        close_vector(m, at, 2);
    }
    if (v12) {
        put_extension_start(m, EXT_EXTENDED_MASTER_SECRET, &at);
        close_vector(m, at, 2);
        put_extension_start(m, EXT_RENEGOTIATION_INFO, &at);
        put8(m, 0);
        close_vector(m, at, 2);
    }
    if (v13) {
        put_extension_start(m, EXT_SUPPORTED_VERSIONS, &at);
        put8(m, v12 ? 4 : 2);
        put16(m, 0x0304);
        if (v12)
            put16(m, 0x0303);
        close_vector(m, at, 2);
        put_extension_start(m, EXT_PSK_MODES, &at);
        put8(m, 1);
        put8(m, 1);
        close_vector(m, at, 2);
        put_extension_start(m, EXT_KEY_SHARE, &at);
        inner = open_vector(m, 2);
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(groups); i++) {
            USHORT g = groups[i];
            bool have = g == GROUP_X25519 ? t->HaveX25519 : g == GROUP_P256 ? t->HaveP256 : t->HaveP384;
            /* after a HelloRetryRequest only the group the server asked for. */
            if (!have || (t->Retried && g != t->Group))
                continue;
            put16(m, g);
            SIZE_T share = open_vector(m, 2);
            put_share(t, m, g);
            close_vector(m, share, 2);
        }
        close_vector(m, inner, 2);
        close_vector(m, at, 2);
        if (t->Cookie.Size) {
            put_extension_start(m, EXT_COOKIE, &at);
            put16(m, t->Cookie.Size);
            TlsBufPut(m, t->Cookie.Data, t->Cookie.Size);
            close_vector(m, at, 2);
        }
    }
    close_vector(m, ext, 2);
    close_vector(m, body, 3);
}

static SECURITY_STATUS send_client_hello(TLS *t)
{
    TLS_BUF m = {};
    build_client_hello(t, &m);
    if (m.Failed) {
        TlsBufFree(&m);
        return no_memory(t);
    }
    add_transcript(t, m.Data, m.Size);
    write_record(t, REC_HANDSHAKE, m.Data, m.Size);
    TlsBufFree(&m);
    return t->Out.Failed || t->Transcript.Failed ? no_memory(t) : SEC_E_OK;
}

/* ------------------------------------------------------------------ setup */

/* the ascii host name of the server name extension: not for addresses, empty when the target is none. */
static void sni_host(TLS *t, PCWSTR target)
{
    t->Host[0] = 0;
    if (!target)
        return;
    SIZE_T n = wcslen(target);
    while (n && target[n - 1] == L'.')
        n--;
    if (!n || n >= sizeof(t->Host))
        return;
    bool digits_and_dots = true;
    for (SIZE_T i = 0; i < n; i++) {
        WCHAR c = target[i];
        if (c <= 0x20 || c >= 0x7F || c == L':' || c == L'/' || c == L'\\' || c == L'[' || c == L']')
            return;
        if (!(c == L'.' || (c >= L'0' && c <= L'9')))
            digits_and_dots = false;
    }
    /* rfc 6066 3: literal addresses are not names. */
    if (digits_and_dots)
        return;
    for (SIZE_T i = 0; i < n; i++)
        t->Host[i] = (char)target[i];
    t->Host[n] = 0;
}

TLS *TlsCreate(const SCH_CONFIG *config, ULONG requested, PCWSTR target)
{
    TLS *t = SecHeapAlloc(sizeof(TLS));
    if (!t)
        return nullptr;
    memset(t, 0, sizeof(*t));
    t->Config = *config;
    t->Requested = requested;
    if (target && target[0]) {
        SIZE_T bytes = (wcslen(target) + 1) * sizeof(WCHAR);
        t->Target = SecHeapAlloc(bytes);
        if (!t->Target) {
            SecHeapFree(t);
            return nullptr;
        }
        memcpy(t->Target, target, bytes);
    }
    sni_host(t, target);
    return t;
}

void TlsDestroy(TLS *t)
{
    if (!t)
        return;
    TlsBufFree(&t->Cookie);
    TlsBufFree(&t->Transcript);
    TlsBufFree(&t->HandshakeIn);
    TlsBufFree(&t->Out);
    SchFreeKey(&t->Key);
    SchFreeCertificates(&t->Certs);
    SecHeapFree(t->Target);
    mime_wipe(t, sizeof(*t));
    SecHeapFree(t);
}

SECURITY_STATUS TlsStart(TLS *t, const BYTE *alpn, SIZE_T alpn_size)
{
    if (!(t->Config.Protocols & (SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT)))
        return SEC_E_ALGORITHM_MISMATCH;
    if (alpn_size > sizeof(t->Alpn))
        return SEC_E_INVALID_TOKEN;
    memcpy(t->Alpn, alpn, alpn_size);
    t->AlpnSize = alpn_size;
    if (!SchRandom(t->ClientRandom, 32))
        return SEC_E_INTERNAL_ERROR;
    if (t->Config.Protocols & SP_PROT_TLS1_3_CLIENT) {
        /* a session id makes the tls 1.3 handshake look like a resumed tls 1.2 one, appendix d.4. */
        t->SessionIdSize = 32;
        if (!SchRandom(t->SessionId, 32) || make_share(t, GROUP_X25519) || make_share(t, GROUP_P256))
            return SEC_E_INTERNAL_ERROR;
    }
    t->State = ST_WAIT_SERVER_HELLO;
    SECURITY_STATUS status = send_client_hello(t);
    return status ? status : SEC_I_CONTINUE_NEEDED;
}

/* ------------------------------------------------------------------ signatures */

static const MimeHashAlg *scheme_hash(unsigned scheme)
{
    switch (scheme & 0xFF) {
    case 0x01:
    case 0x03:
        switch (scheme >> 8) {
        case 4:
            return &mime_sha256;
        case 5:
            return &mime_sha384;
        case 6:
            return &mime_sha512;
        }
        return nullptr;
    }
    switch (scheme) {
    case 0x0804:
        return &mime_sha256;
    case 0x0805:
        return &mime_sha384;
    case 0x0806:
        return &mime_sha512;
    }
    return nullptr;
}

static bool scheme_offered(unsigned scheme)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(schemes); i++)
        if (schemes[i] == scheme)
            return true;
    return false;
}

/* a der INTEGER of the ecdsa signature into a fixed size big endian field. */
static bool der_integer(READER *r, BYTE *out, SIZE_T size)
{
    unsigned tag, length;
    const BYTE *p;
    if (!read_int(r, 1, &tag) || tag != 0x02 || !read_int(r, 1, &length) || length > 0x7F || !take(r, length, &p))
        return false;
    while (length && !p[0]) {
        p++;
        length--;
    }
    if (length > size)
        return false;
    memset(out, 0, size - length);
    memcpy(out + size - length, p, length);
    return true;
}

/* the Ecdsa-Sig-Value SEQUENCE of r and s, as r || s. */
static bool ecdsa_raw(const BYTE *der, SIZE_T der_size, BYTE *raw, SIZE_T half)
{
    READER r = {der, der_size}, seq;
    unsigned tag, length;
    if (!read_int(&r, 1, &tag) || tag != 0x30 || !read_int(&r, 1, &length))
        return false;
    if (length == 0x81 && !read_int(&r, 1, &length))
        return false;
    else if (length > 0x81)
        return false;
    seq.P = r.P;
    seq.Left = length;
    if (!take(&r, length, nullptr) || r.Left)
        return false;
    return der_integer(&seq, raw, half) && der_integer(&seq, raw + half, half) && !seq.Left;
}

/* verifies the server's signature over content with the leaf key; tls 1.3 refuses pkcs #1 v1.5. */
static bool verify_signature(TLS *t, unsigned scheme, const BYTE *content, SIZE_T content_size, const BYTE *sig,
                             SIZE_T sig_size)
{
    const MimeHashAlg *h = scheme_hash(scheme);
    if (!h || !scheme_offered(scheme))
        return false;
    BYTE digest[64];
    mime_hash(h, content, content_size, digest);
    if ((scheme & 0xFF) == 0x03) {
        if (t->Key.Type != 2)
            return false;
        /* tls 1.3 ties each ecdsa scheme to its curve, tls 1.2 only to the hash. */
        if (is13(t)) {
            const MimeEcCurve *want = scheme == 0x0403 ? &mime_p256 : scheme == 0x0503 ? &mime_p384 : &mime_p521;
            if (t->Key.Ec.curve != want)
                return false;
        }
        BYTE raw[132];
        SIZE_T half = mime_ec_size(t->Key.Ec.curve);
        return ecdsa_raw(sig, sig_size, raw, half) &&
               mime_ecdsa_verify(&t->Key.Ec, digest, h->digest_size, raw, 2 * half) == MIME_CRYPTO_OK;
    }
    if (t->Key.Type != 1)
        return false;
    if ((scheme & 0xFF00) == 0x0800)
        return mime_rsa_pss_verify(t->Key.Rsa, h, digest, h->digest_size, h->digest_size, sig, sig_size) ==
               MIME_CRYPTO_OK;
    if (is13(t))
        return false;
    return mime_rsa_pkcs1_verify(t->Key.Rsa, h, digest, h->digest_size, sig, sig_size) == MIME_CRYPTO_OK;
}

/* ------------------------------------------------------------------ server messages */

/* the extension types a server may send back, each at most once. */
typedef struct EXTENSIONS {
    UINT64 Seen;
    READER Versions, KeyShare, Cookie, Alpn, Renegotiation;
    bool HaveVersions, HaveKeyShare, HaveCookie, HaveAlpn, HaveEms, HaveRenegotiation;
} EXTENSIONS;

static int extension_bit(unsigned type)
{
    switch (type) {
    case EXT_SERVER_NAME:
        return 0;
    case EXT_SUPPORTED_GROUPS:
        return 1;
    case EXT_EC_POINT_FORMATS:
        return 2;
    case EXT_ALPN:
        return 3;
    case EXT_EXTENDED_MASTER_SECRET:
        return 4;
    case EXT_SUPPORTED_VERSIONS:
        return 5;
    case EXT_COOKIE:
        return 6;
    case EXT_KEY_SHARE:
        return 7;
    case EXT_RENEGOTIATION_INFO:
        return 8;
    }
    return -1;
}

/* reads an extension block, refusing duplicates and types outside allowed. */
static BYTE read_extensions(READER *block, UINT64 allowed, EXTENSIONS *e)
{
    memset(e, 0, sizeof(*e));
    while (block->Left) {
        unsigned type;
        READER data;
        if (!read_int(block, 2, &type) || !read_vector(block, 2, &data))
            return ALERT_DECODE_ERROR;
        int bit = extension_bit(type);
        if (bit < 0 || !(allowed & (1ull << bit)))
            return ALERT_UNSUPPORTED_EXTENSION;
        if (e->Seen & (1ull << bit))
            return ALERT_ILLEGAL_PARAMETER;
        e->Seen |= 1ull << bit;
        switch (type) {
        case EXT_SUPPORTED_VERSIONS:
            e->Versions = data;
            e->HaveVersions = true;
            break;
        case EXT_KEY_SHARE:
            e->KeyShare = data;
            e->HaveKeyShare = true;
            break;
        case EXT_COOKIE:
            e->Cookie = data;
            e->HaveCookie = true;
            break;
        case EXT_ALPN:
            e->Alpn = data;
            e->HaveAlpn = true;
            break;
        case EXT_EXTENDED_MASTER_SECRET:
            if (data.Left)
                return ALERT_DECODE_ERROR;
            e->HaveEms = true;
            break;
        case EXT_RENEGOTIATION_INFO:
            e->Renegotiation = data;
            e->HaveRenegotiation = true;
            break;
        case EXT_SERVER_NAME:
            if (data.Left)
                return ALERT_DECODE_ERROR;
            break;
        }
    }
    return 0;
}

#define EXT(type) (1ull << extension_bit(type))

/* the protocol the server picked must be one name out of the offered list. */
static BYTE take_alpn(TLS *t, READER data)
{
    READER list, name;
    if (!t->AlpnSize)
        return ALERT_UNSUPPORTED_EXTENSION;
    if (!read_vector(&data, 2, &list) || data.Left || !read_vector(&list, 1, &name) || list.Left || !name.Left)
        return ALERT_DECODE_ERROR;
    READER offered = {t->Alpn, t->AlpnSize}, entry;
    while (read_vector(&offered, 1, &entry)) {
        if (entry.Left == name.Left && !memcmp(entry.P, name.P, name.Left)) {
            memcpy(t->Protocol, name.P, name.Left);
            t->ProtocolSize = (BYTE)name.Left;
            return 0;
        }
    }
    return ALERT_ILLEGAL_PARAMETER;
}

static SECURITY_STATUS on_hello_retry(TLS *t, const BYTE *raw, SIZE_T raw_size, const EXTENSIONS *e)
{
    unsigned group;
    if (t->Retried)
        return fail(t, ALERT_UNEXPECTED_MESSAGE, 0);
    t->Retried = true;
    if (!e->HaveKeyShare && !e->HaveCookie)
        return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    if (e->HaveKeyShare) {
        READER k = e->KeyShare;
        if (!read_int(&k, 2, &group) || k.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
        bool sent = (group == GROUP_X25519 && t->HaveX25519) || (group == GROUP_P256 && t->HaveP256) ||
                    (group == GROUP_P384 && t->HaveP384);
        if (!group_offered((USHORT)group) || sent)
            return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
        t->Group = (USHORT)group;
        if (make_share(t, t->Group))
            return fail(t, ALERT_INTERNAL_ERROR, SEC_E_INTERNAL_ERROR);
    } else {
        t->Group = t->HaveX25519 ? GROUP_X25519 : GROUP_P256;
    }
    if (e->HaveCookie) {
        READER c = e->Cookie, value;
        if (!read_vector(&c, 2, &value) || c.Left || !value.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
        TlsBufPut(&t->Cookie, value.P, value.Left);
    }
    /* 4.4.1: the first ClientHello becomes a message_hash of it. */
    BYTE hash[48];
    UINT hl = hash_size(t);
    transcript_hash(t, hash);
    t->Transcript.Size = 0;
    BYTE head[4] = {HS_MESSAGE_HASH, 0, 0, (BYTE)hl};
    add_transcript(t, head, 4);
    add_transcript(t, hash, hl);
    add_transcript(t, raw, raw_size);
    if (!t->CcsSent) {
        static const BYTE one = 1;
        write_record(t, REC_CCS, &one, 1);
        t->CcsSent = true;
    }
    t->State = ST_WAIT_SERVER_HELLO;
    return send_client_hello(t);
}

static SECURITY_STATUS on_server_hello(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    unsigned legacy, suite_id, compression;
    const BYTE *random;
    READER session, block = {};
    if (!read_int(&body, 2, &legacy) || !take(&body, 32, &random) || !read_vector(&body, 1, &session) ||
        session.Left > 32 || !read_int(&body, 2, &suite_id) || !read_int(&body, 1, &compression))
        return fail(t, ALERT_DECODE_ERROR, 0);
    if (body.Left && (!read_vector(&body, 2, &block) || body.Left))
        return fail(t, ALERT_DECODE_ERROR, 0);
    bool retry = !memcmp(random, hello_retry_random, 32);
    EXTENSIONS e;
    UINT64 allowed13 = EXT(EXT_SUPPORTED_VERSIONS) | EXT(EXT_KEY_SHARE) | (retry ? EXT(EXT_COOKIE) : 0);
    UINT64 allowed12 = EXT(EXT_SERVER_NAME) | EXT(EXT_EC_POINT_FORMATS) | EXT(EXT_ALPN) |
                       EXT(EXT_EXTENDED_MASTER_SECRET) | EXT(EXT_RENEGOTIATION_INFO) | EXT(EXT_SUPPORTED_VERSIONS);
    BYTE alert = read_extensions(&block, allowed13 | allowed12, &e);
    if (alert)
        return fail(t, alert, 0);

    unsigned version = legacy;
    if (e.HaveVersions) {
        READER v = e.Versions;
        if (!read_int(&v, 2, &version) || v.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
        if (version != 0x0304 || legacy != 0x0303 || !(t->Config.Protocols & SP_PROT_TLS1_3_CLIENT))
            return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    }
    if (t->Version && version != t->Version)
        return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    if (version == 0x0304) {
        if (e.Seen & ~allowed13)
            return fail(t, ALERT_UNSUPPORTED_EXTENSION, 0);
    } else if (version == 0x0303 && (t->Config.Protocols & SP_PROT_TLS1_2_CLIENT) && !retry) {
        if (e.Seen & ~allowed12)
            return fail(t, ALERT_UNSUPPORTED_EXTENSION, 0);
        /* 4.1.3: a tls 1.2 answer to a client offering 1.3 must not carry the downgrade mark. */
        if ((t->Config.Protocols & SP_PROT_TLS1_3_CLIENT) && !memcmp(random + 24, downgrade_12, 8))
            return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    } else {
        return fail(t, ALERT_PROTOCOL_VERSION, 0);
    }

    const TLS_SUITE *suite = find_suite(t, suite_id);
    if (!suite || suite->Version != version || compression)
        return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    if (t->Suite && t->Suite != suite)
        return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    t->Version = (USHORT)version;
    t->Suite = suite;
    memcpy(t->ServerSessionId, session.P, session.Left);
    t->ServerSessionIdSize = (BYTE)session.Left;

    if (version == 0x0304) {
        if (session.Left != t->SessionIdSize || memcmp(session.P, t->SessionId, session.Left))
            return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
        if (retry)
            return on_hello_retry(t, raw, raw_size, &e);
        memcpy(t->ServerRandom, random, 32);
        unsigned group;
        READER share, k = e.KeyShare;
        if (!e.HaveKeyShare)
            return fail(t, ALERT_MISSING_EXTENSION, 0);
        if (!read_int(&k, 2, &group) || !read_vector(&k, 2, &share) || k.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
        bool sent = (group == GROUP_X25519 && t->HaveX25519) || (group == GROUP_P256 && t->HaveP256) ||
                    (group == GROUP_P384 && t->HaveP384);
        if (!sent || (t->Retried && group != t->Group))
            return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
        t->PeerGroup = (USHORT)group;
        BYTE secret[66];
        SIZE_T secret_size;
        if (!agree(t, (USHORT)group, share.P, share.Left, secret, &secret_size))
            return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
        add_transcript(t, raw, raw_size);
        derive_handshake13(t, secret, secret_size);
        mime_wipe(secret, sizeof(secret));
        t->State = ST13_WAIT_EXTENSIONS;
        return SEC_E_OK;
    }

    memcpy(t->ServerRandom, random, 32);
    if (e.HaveRenegotiation) {
        READER r = e.Renegotiation, value;
        if (!read_vector(&r, 1, &value) || r.Left || value.Left)
            return fail(t, ALERT_HANDSHAKE_FAILURE, 0);
    }
    if (e.HaveAlpn && (alert = take_alpn(t, e.Alpn)))
        return fail(t, alert, 0);
    t->Ems = e.HaveEms;
    add_transcript(t, raw, raw_size);
    t->State = ST12_WAIT_CERTIFICATE;
    return SEC_E_OK;
}

static SECURITY_STATUS on_encrypted_extensions(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    READER block;
    EXTENSIONS e;
    if (!read_vector(&body, 2, &block) || body.Left)
        return fail(t, ALERT_DECODE_ERROR, 0);
    BYTE alert = read_extensions(&block, EXT(EXT_SERVER_NAME) | EXT(EXT_SUPPORTED_GROUPS) | EXT(EXT_ALPN), &e);
    if (!alert && e.HaveAlpn)
        alert = take_alpn(t, e.Alpn);
    if (alert)
        return fail(t, alert, 0);
    add_transcript(t, raw, raw_size);
    t->State = ST13_WAIT_CERTIFICATE_OR_REQUEST;
    return SEC_E_OK;
}

static SECURITY_STATUS on_certificate_request(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    if (is13(t)) {
        READER context, block;
        if (!read_vector(&body, 1, &context) || !read_vector(&body, 2, &block) || body.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
        memcpy(t->RequestContext, context.P, context.Left);
        t->RequestContextSize = (BYTE)context.Left;
        t->State = ST13_WAIT_CERTIFICATE;
    } else {
        READER types, algorithms, authorities;
        if (!read_vector(&body, 1, &types) || !read_vector(&body, 2, &algorithms) ||
            !read_vector(&body, 2, &authorities) || body.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
    }
    /* mime has no client certificates: the answer is an empty Certificate, as with SCH_CRED_NO_DEFAULT_CREDS. */
    t->CertRequested = true;
    add_transcript(t, raw, raw_size);
    return SEC_E_OK;
}

static SECURITY_STATUS on_certificate(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    READER list;
    if (is13(t)) {
        READER context;
        if (!read_vector(&body, 1, &context) || context.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
    }
    if (!read_vector(&body, 3, &list) || body.Left || !list.Left)
        return fail(t, ALERT_DECODE_ERROR, 0);
    const BYTE *der[16];
    SIZE_T sizes[16];
    DWORD count = 0;
    while (list.Left) {
        READER cert, extensions;
        if (!read_vector(&list, 3, &cert) || !cert.Left)
            return fail(t, ALERT_DECODE_ERROR, 0);
        if (is13(t) && !read_vector(&list, 2, &extensions))
            return fail(t, ALERT_DECODE_ERROR, 0);
        /* a chain longer than any real one: the rest is ignored. */
        if (count < RTL_NUMBER_OF(der)) {
            der[count] = cert.P;
            sizes[count] = cert.Left;
            count++;
        }
    }
    SECURITY_STATUS status = SchLoadCertificates(&t->Certs, der, sizes, count);
    if (status)
        return fail(t, ALERT_BAD_CERTIFICATE, status);
    status = SchLeafKey(&t->Certs, &t->Key);
    if (status)
        return fail(t, ALERT_UNSUPPORTED_CERTIFICATE, status);
    /* the suite of tls 1.2 names the key type. */
    if (!is13(t) && (t->Key.Type == 2) != t->Suite->Ecdsa)
        return fail(t, ALERT_UNSUPPORTED_CERTIFICATE, SEC_E_CERT_UNKNOWN);
    add_transcript(t, raw, raw_size);
    t->State = is13(t) ? ST13_WAIT_VERIFY : ST12_WAIT_KEY_EXCHANGE;
    return SEC_E_OK;
}

/* the chain checks of crypt32, unless the caller validates the certificate itself. */
static SECURITY_STATUS validate(TLS *t)
{
    if ((t->Config.Flags & SCH_CRED_MANUAL_CRED_VALIDATION) || (t->Requested & ISC_REQ_MANUAL_CRED_VALIDATION))
        return SEC_E_OK;
    PCWSTR name = (t->Config.Flags & SCH_CRED_NO_SERVERNAME_CHECK) ? nullptr : t->Target;
    BYTE alert = ALERT_BAD_CERTIFICATE;
    SECURITY_STATUS status = SchValidateChain(&t->Certs, name, t->Config.Flags, &alert);
    return status ? fail(t, alert, status) : SEC_E_OK;
}

static SECURITY_STATUS on_certificate_verify(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    unsigned scheme;
    READER sig;
    if (!read_int(&body, 2, &scheme) || !read_vector(&body, 2, &sig) || body.Left)
        return fail(t, ALERT_DECODE_ERROR, 0);
    /* 4.4.3: 64 spaces, the context string, a zero byte and the transcript hash. */
    static const char label[] = "TLS 1.3, server CertificateVerify";
    BYTE content[64 + sizeof(label) + 48];
    memset(content, 0x20, 64);
    memcpy(content + 64, label, sizeof(label));
    transcript_hash(t, content + 64 + sizeof(label));
    if (!verify_signature(t, scheme, content, 64 + sizeof(label) + hash_size(t), sig.P, sig.Left))
        return fail(t, ALERT_DECRYPT_ERROR, 0);
    SECURITY_STATUS status = validate(t);
    if (status)
        return status;
    add_transcript(t, raw, raw_size);
    t->State = ST13_WAIT_FINISHED;
    return SEC_E_OK;
}

static void send_message(TLS *t, const BYTE *message, SIZE_T size)
{
    add_transcript(t, message, size);
    write_record(t, REC_HANDSHAKE, message, size);
}

static void send_ccs(TLS *t)
{
    static const BYTE one = 1;
    write_record(t, REC_CCS, &one, 1);
    t->CcsSent = true;
}

static SECURITY_STATUS on_finished13(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    UINT hl = hash_size(t);
    BYTE th[48], expected[48];
    if (body.Left != hl)
        return fail(t, ALERT_DECODE_ERROR, 0);
    transcript_hash(t, th);
    finished_mac(t, t->ServerHandshake, th, expected);
    if (!mime_ct_equal(expected, body.P, hl))
        return fail(t, ALERT_DECRYPT_ERROR, 0);
    add_transcript(t, raw, raw_size);
    transcript_hash(t, th);
    expand_label(t->Suite->Hash, t->Master, "c ap traffic", th, hl, t->ClientTraffic, hl);
    expand_label(t->Suite->Hash, t->Master, "s ap traffic", th, hl, t->ServerTraffic, hl);

    if (!t->CcsSent)
        send_ccs(t);
    set_keys13(t, &t->Write, t->ClientHandshake);
    if (t->CertRequested) {
        BYTE m[4 + 1 + 255 + 3];
        SIZE_T n = 0;
        m[n++] = HS_CERTIFICATE;
        m[n++] = 0;
        m[n++] = 0;
        m[n++] = (BYTE)(1 + t->RequestContextSize + 3);
        m[n++] = t->RequestContextSize;
        memcpy(m + n, t->RequestContext, t->RequestContextSize);
        n += t->RequestContextSize;
        m[n++] = 0;
        m[n++] = 0;
        m[n++] = 0;
        send_message(t, m, n);
    }
    BYTE fin[4 + 48] = {HS_FINISHED, 0, 0, (BYTE)hl};
    transcript_hash(t, th);
    finished_mac(t, t->ClientHandshake, th, fin + 4);
    send_message(t, fin, 4 + hl);
    set_keys13(t, &t->Write, t->ClientTraffic);
    set_keys13(t, &t->Read, t->ServerTraffic);
    if (t->Out.Failed || t->Transcript.Failed)
        return no_memory(t);
    t->State = ST_CONNECTED;
    return SEC_E_OK;
}

static SECURITY_STATUS on_server_key_exchange(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    const BYTE *params = body.P;
    unsigned curve_type, group, scheme;
    READER point, sig;
    if (!read_int(&body, 1, &curve_type) || !read_int(&body, 2, &group) || !read_vector(&body, 1, &point))
        return fail(t, ALERT_DECODE_ERROR, 0);
    SIZE_T params_size = body.P - params;
    if (!read_int(&body, 2, &scheme) || !read_vector(&body, 2, &sig) || body.Left)
        return fail(t, ALERT_DECODE_ERROR, 0);
    if (curve_type != 3 || !group_offered((USHORT)group))
        return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    /* 5246 7.4.3: the signature covers both randoms and the parameters. */
    BYTE content[64 + 4 + 133];
    if (params_size > sizeof(content) - 64)
        return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    memcpy(content, t->ClientRandom, 32);
    memcpy(content + 32, t->ServerRandom, 32);
    memcpy(content + 64, params, params_size);
    bool ecdsa_scheme = (scheme & 0xFF) == 0x03;
    if (ecdsa_scheme != t->Suite->Ecdsa || !verify_signature(t, scheme, content, 64 + params_size, sig.P, sig.Left))
        return fail(t, ALERT_DECRYPT_ERROR, 0);
    t->Group = (USHORT)group;
    t->PeerGroup = (USHORT)group;
    if (make_share(t, t->Group))
        return fail(t, ALERT_INTERNAL_ERROR, SEC_E_INTERNAL_ERROR);
    /* keep the peer's point in the cookie buffer, which tls 1.2 does not use. */
    t->Cookie.Size = 0;
    TlsBufPut(&t->Cookie, point.P, point.Left);
    add_transcript(t, raw, raw_size);
    t->State = ST12_WAIT_DONE;
    return SEC_E_OK;
}

static SECURITY_STATUS on_server_hello_done(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    if (body.Left)
        return fail(t, ALERT_DECODE_ERROR, 0);
    SECURITY_STATUS status = validate(t);
    if (status)
        return status;
    add_transcript(t, raw, raw_size);

    BYTE secret[66];
    SIZE_T secret_size;
    if (!agree(t, t->Group, t->Cookie.Data, t->Cookie.Size, secret, &secret_size))
        return fail(t, ALERT_ILLEGAL_PARAMETER, 0);
    if (t->CertRequested) {
        static const BYTE empty[7] = {HS_CERTIFICATE, 0, 0, 3, 0, 0, 0};
        send_message(t, empty, sizeof(empty));
    }
    TLS_BUF m = {};
    put8(&m, HS_CLIENT_KEY_EXCHANGE);
    SIZE_T body_at = open_vector(&m, 3);
    SIZE_T point_at = open_vector(&m, 1);
    put_share(t, &m, t->Group);
    close_vector(&m, point_at, 1);
    close_vector(&m, body_at, 3);
    if (m.Failed) {
        TlsBufFree(&m);
        return no_memory(t);
    }
    send_message(t, m.Data, m.Size);
    TlsBufFree(&m);

    BYTE seed[64];
    if (t->Ems) {
        BYTE session_hash[48];
        transcript_hash(t, session_hash);
        prf12(t, secret, secret_size, "extended master secret", session_hash, hash_size(t), t->Master, 48);
    } else {
        memcpy(seed, t->ClientRandom, 32);
        memcpy(seed + 32, t->ServerRandom, 32);
        prf12(t, secret, secret_size, "master secret", seed, 64, t->Master, 48);
    }
    mime_wipe(secret, sizeof(secret));
    /* 6.3: the key block, client then server keys, then the implicit nonce parts. */
    BYTE block[2 * 32 + 2 * 4];
    UINT k = t->Suite->KeySize;
    memcpy(seed, t->ServerRandom, 32);
    memcpy(seed + 32, t->ClientRandom, 32);
    prf12(t, t->Master, 48, "key expansion", seed, 64, block, 2 * k + 8);
    mime_aes_init(&t->Write.Aes, block, k);
    memcpy(t->Write.Iv, block + 2 * k, 4);
    mime_aes_init(&t->PendingRead.Aes, block + k, k);
    memcpy(t->PendingRead.Iv, block + 2 * k + 4, 4);
    mime_wipe(block, sizeof(block));

    send_ccs(t);
    t->Write.Seq = 0;
    t->Write.Active = true;
    BYTE th[48], fin[4 + 12] = {HS_FINISHED, 0, 0, 12};
    transcript_hash(t, th);
    prf12(t, t->Master, 48, "client finished", th, hash_size(t), fin + 4, 12);
    send_message(t, fin, sizeof(fin));
    if (t->Out.Failed || t->Transcript.Failed)
        return no_memory(t);
    t->State = ST12_WAIT_CCS;
    return SEC_E_OK;
}

static SECURITY_STATUS on_finished12(TLS *t, READER body, const BYTE *raw, SIZE_T raw_size)
{
    BYTE th[48], expected[12];
    if (body.Left != 12)
        return fail(t, ALERT_DECODE_ERROR, 0);
    transcript_hash(t, th);
    prf12(t, t->Master, 48, "server finished", th, hash_size(t), expected, 12);
    if (!mime_ct_equal(expected, body.P, 12))
        return fail(t, ALERT_DECRYPT_ERROR, 0);
    add_transcript(t, raw, raw_size);
    t->State = ST_CONNECTED;
    return SEC_E_OK;
}

static SECURITY_STATUS on_message(TLS *t, unsigned type, READER body, const BYTE *raw, SIZE_T raw_size)
{
    switch (t->State) {
    case ST_WAIT_SERVER_HELLO:
        if (type == HS_SERVER_HELLO)
            return on_server_hello(t, body, raw, raw_size);
        break;
    case ST13_WAIT_EXTENSIONS:
        if (type == HS_ENCRYPTED_EXTENSIONS)
            return on_encrypted_extensions(t, body, raw, raw_size);
        break;
    case ST13_WAIT_CERTIFICATE_OR_REQUEST:
        if (type == HS_CERTIFICATE_REQUEST)
            return on_certificate_request(t, body, raw, raw_size);
        [[fallthrough]];
    case ST13_WAIT_CERTIFICATE:
    case ST12_WAIT_CERTIFICATE:
        if (type == HS_CERTIFICATE)
            return on_certificate(t, body, raw, raw_size);
        break;
    case ST13_WAIT_VERIFY:
        if (type == HS_CERTIFICATE_VERIFY)
            return on_certificate_verify(t, body, raw, raw_size);
        break;
    case ST13_WAIT_FINISHED:
        if (type == HS_FINISHED)
            return on_finished13(t, body, raw, raw_size);
        break;
    case ST12_WAIT_KEY_EXCHANGE:
        if (type == HS_SERVER_KEY_EXCHANGE)
            return on_server_key_exchange(t, body, raw, raw_size);
        break;
    case ST12_WAIT_DONE:
        if (type == HS_CERTIFICATE_REQUEST && !t->CertRequested)
            return on_certificate_request(t, body, raw, raw_size);
        if (type == HS_SERVER_HELLO_DONE)
            return on_server_hello_done(t, body, raw, raw_size);
        break;
    case ST12_WAIT_FINISHED:
        if (type == HS_FINISHED)
            return on_finished12(t, body, raw, raw_size);
        break;
    }
    /* 5246 7.4.1.1: a HelloRequest during the handshake is ignored. */
    if (type == HS_HELLO_REQUEST && !is13(t) && !body.Left)
        return SEC_E_OK;
    return fail(t, ALERT_UNEXPECTED_MESSAGE, 0);
}

/* the complete messages waiting in HandshakeIn. */
static SECURITY_STATUS run_messages(TLS *t)
{
    while (t->HandshakeIn.Size >= 4 && t->State != ST_FAILED && t->State != ST_CONNECTED) {
        BYTE *p = t->HandshakeIn.Data;
        SIZE_T length = (SIZE_T)p[1] << 16 | (SIZE_T)p[2] << 8 | p[3];
        if (length > TLS_MAX_MESSAGE)
            return fail(t, ALERT_DECODE_ERROR, 0);
        if (t->HandshakeIn.Size < 4 + length)
            break;
        int before = t->State;
        READER body = {p + 4, length};
        SECURITY_STATUS status = on_message(t, p[0], body, p, 4 + length);
        if (status)
            return status;
        drop_front(&t->HandshakeIn, 4 + length);
        /* 8446 5.1: a key change must fall on a record boundary. */
        bool rekeyed = (before == ST_WAIT_SERVER_HELLO && is13(t)) || t->State == ST_CONNECTED;
        if (rekeyed && t->HandshakeIn.Size)
            return fail(t, ALERT_UNEXPECTED_MESSAGE, 0);
    }
    return SEC_E_OK;
}

/* ------------------------------------------------------------------ the handshake loop */

static SECURITY_STATUS on_alert(TLS *t, const BYTE *body, SIZE_T size, bool *stop)
{
    *stop = false;
    if (size != 2 || (body[0] != TLS1_ALERT_WARNING && body[0] != TLS1_ALERT_FATAL)) {
        *stop = true;
        return fail(t, ALERT_DECODE_ERROR, SEC_E_INVALID_TOKEN);
    }
    SECURITY_STATUS status = alert_status(body[0], body[1]);
    if (status == SEC_E_OK || status == SEC_I_NO_RENEGOTIATION)
        return SEC_E_OK;
    *stop = true;
    t->State = ST_FAILED;
    t->Error = status == SEC_I_CONTEXT_EXPIRED ? SEC_E_CONTEXT_EXPIRED : status;
    return t->Error;
}

static SECURITY_STATUS handshake_record(TLS *t, BYTE *record, SIZE_T total)
{
    BYTE type = record[0];
    BYTE *plain = record + 5;
    SIZE_T size = total - 5;
    if (type == REC_CCS) {
        if (size != 1 || plain[0] != 1)
            return fail(t, ALERT_UNEXPECTED_MESSAGE, 0);
        /* d.4: the compatibility ChangeCipherSpec of a tls 1.3 server is dropped. */
        if (is13(t) && t->State >= ST_WAIT_SERVER_HELLO && t->State <= ST13_WAIT_FINISHED)
            return SEC_E_OK;
        if (t->State == ST12_WAIT_CCS && !t->HandshakeIn.Size) {
            t->Read = t->PendingRead;
            t->Read.Seq = 0;
            t->Read.Active = true;
            t->State = ST12_WAIT_FINISHED;
            return SEC_E_OK;
        }
        return fail(t, ALERT_UNEXPECTED_MESSAGE, 0);
    }
    if (t->Read.Active && type != REC_ALERT) {
        BYTE alert;
        SECURITY_STATUS status = open_record(t, record, total, &plain, &size, &type, &alert);
        if (status)
            return fail(t, alert, status);
    } else if (t->Read.Active && is13(t)) {
        /* a plain alert from a server that could not go on. */
    } else if (t->Read.Active) {
        BYTE alert;
        SECURITY_STATUS status = open_record(t, record, total, &plain, &size, &type, &alert);
        if (status)
            return fail(t, alert, status);
    } else if (size > TLS_MAX_PLAIN) {
        return fail(t, ALERT_RECORD_OVERFLOW, 0);
    }
    if (type == REC_ALERT) {
        bool stop;
        return on_alert(t, plain, size, &stop);
    }
    if (type != REC_HANDSHAKE || !size)
        return fail(t, ALERT_UNEXPECTED_MESSAGE, 0);
    if (!TlsBufPut(&t->HandshakeIn, plain, size))
        return no_memory(t);
    return run_messages(t);
}

static bool record_version_ok(const BYTE *header)
{
    return header[1] == 3 && header[2] <= 4;
}

SECURITY_STATUS TlsHandshake(TLS *t, BYTE *input, SIZE_T size, SIZE_T *consumed, SIZE_T *missing)
{
    *consumed = 0;
    *missing = 0;
    if (t->State == ST_FAILED)
        return t->Error;
    if (t->AlertPending) {
        /* a shutdown before the end of the handshake. */
        BYTE body[2] = {t->AlertLevel, t->AlertNumber};
        write_record(t, REC_ALERT, body, 2);
        t->AlertPending = false;
        t->State = ST_FAILED;
        t->Error = SEC_E_CONTEXT_EXPIRED;
        return SEC_E_OK;
    }
    SIZE_T at = 0;
    bool any = false;
    while (t->State != ST_CONNECTED) {
        SIZE_T left = size - at;
        if (left < 5) {
            *missing = 5 - left;
            break;
        }
        BYTE *record = input + at;
        SIZE_T length = get16(record + 3);
        if (!record_version_ok(record) || record[0] < REC_CCS || record[0] > REC_DATA)
            return fail(t, ALERT_DECODE_ERROR, SEC_E_ILLEGAL_MESSAGE);
        if (length > TLS_MAX_PLAIN + 2048)
            return fail(t, ALERT_RECORD_OVERFLOW, 0);
        if (left < 5 + length) {
            *missing = 5 + length - left;
            break;
        }
        SECURITY_STATUS status = handshake_record(t, record, 5 + length);
        at += 5 + length;
        any = true;
        if (status)
            return status;
    }
    *consumed = at;
    if (t->State == ST_CONNECTED)
        return SEC_E_OK;
    if (!any)
        return SEC_E_INCOMPLETE_MESSAGE;
    *missing = 0;
    return SEC_I_CONTINUE_NEEDED;
}

TLS_BUF *TlsOutput(TLS *t)
{
    return &t->Out;
}

bool TlsConnected(const TLS *t)
{
    return t->State == ST_CONNECTED;
}

/* ------------------------------------------------------------------ after the handshake */

static void key_update(TLS *t, TLS_CIPHER *c, BYTE *secret)
{
    UINT hl = hash_size(t);
    BYTE next[48];
    expand_label(t->Suite->Hash, secret, "traffic upd", nullptr, 0, next, hl);
    memcpy(secret, next, hl);
    set_keys13(t, c, secret);
    mime_wipe(next, sizeof(next));
}

/* our KeyUpdate answering the server's request, protected with the old keys. */
static void send_key_update(TLS *t)
{
    static const BYTE message[5] = {HS_KEY_UPDATE, 0, 0, 1, 0};
    write_record(t, REC_HANDSHAKE, message, sizeof(message));
    key_update(t, &t->Write, t->ClientTraffic);
    t->KeyUpdatePending = false;
}

SECURITY_STATUS TlsPostHandshake(TLS *t, SIZE_T size, SIZE_T *consumed)
{
    *consumed = 0;
    if (t->PendingSkip && size >= t->PendingSkip)
        *consumed = t->PendingSkip;
    t->PendingSkip = 0;
    if (t->KeyUpdatePending && !t->SentClose)
        send_key_update(t);
    if (t->AlertPending && !t->SentClose) {
        BYTE body[2] = {t->AlertLevel, t->AlertNumber};
        write_record(t, REC_ALERT, body, 2);
        t->AlertPending = false;
        t->SentClose = true;
    }
    return t->Out.Failed ? SEC_E_INSUFFICIENT_MEMORY : SEC_E_OK;
}

void TlsQueueAlert(TLS *t, BYTE level, BYTE number)
{
    t->AlertPending = true;
    t->AlertLevel = level;
    t->AlertNumber = number;
}

/* the messages a tls 1.3 server may send after the handshake. */
static SECURITY_STATUS post_handshake_messages(TLS *t)
{
    while (t->HandshakeIn.Size >= 4) {
        BYTE *p = t->HandshakeIn.Data;
        SIZE_T length = (SIZE_T)p[1] << 16 | (SIZE_T)p[2] << 8 | p[3];
        if (length > TLS_MAX_MESSAGE)
            return SEC_E_ILLEGAL_MESSAGE;
        if (t->HandshakeIn.Size < 4 + length)
            break;
        READER body = {p + 4, length};
        if (p[0] == HS_NEW_SESSION_TICKET) {
            /* 4.6.1: checked and dropped, mime does not resume sessions. */
            unsigned lifetime, age;
            READER nonce, ticket, extensions;
            if (!read_int(&body, 4, &lifetime) || !read_int(&body, 4, &age) || !read_vector(&body, 1, &nonce) ||
                !read_vector(&body, 2, &ticket) || !ticket.Left || !read_vector(&body, 2, &extensions) || body.Left)
                return SEC_E_ILLEGAL_MESSAGE;
        } else if (p[0] == HS_KEY_UPDATE) {
            if (length != 1 || p[4] > 1)
                return SEC_E_ILLEGAL_MESSAGE;
            if (t->HandshakeIn.Size != 5)
                return SEC_E_INVALID_TOKEN;
            key_update(t, &t->Read, t->ServerTraffic);
            if (p[4])
                t->KeyUpdatePending = true;
        } else {
            return SEC_E_INVALID_TOKEN;
        }
        drop_front(&t->HandshakeIn, 4 + length);
    }
    return SEC_E_OK;
}

SECURITY_STATUS TlsDecrypt(TLS *t, BYTE *input, SIZE_T size, TLS_RECORD *record, SIZE_T *missing)
{
    *missing = 0;
    memset(record, 0, sizeof(*record));
    if (t->State != ST_CONNECTED)
        return t->State == ST_FAILED ? t->Error : SEC_E_INVALID_HANDLE;
    SIZE_T head = seal_header_size(t);
    if (t->PendingSkip) {
        /* the caller went on without InitializeSecurityContext: the handed over record is done. */
        SIZE_T skip = t->PendingSkip;
        t->PendingSkip = 0;
        if (size >= skip) {
            record->Header = input;
            record->HeaderSize = head;
            record->Data = input + head;
            record->Total = skip;
            return SEC_E_OK;
        }
    }
    if (size < head) {
        *missing = head - size;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    SIZE_T total = 5 + get16(input + 3);
    if (total < head || !record_version_ok(input))
        return SEC_E_INVALID_TOKEN;
    if (size < total) {
        *missing = total - size;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    BYTE *plain, type, alert;
    SIZE_T plain_size;
    SECURITY_STATUS status;
    if (input[0] == REC_CCS) {
        return SEC_E_ILLEGAL_MESSAGE;
    } else if ((status = open_record(t, input, total, &plain, &plain_size, &type, &alert))) {
        return status;
    }
    record->Header = input;
    record->HeaderSize = head;
    record->Data = plain;
    record->DataSize = plain_size;
    record->Total = total;
    record->TrailerSize = total - head - plain_size;
    switch (type) {
    case REC_DATA:
        if (t->ReceivedClose)
            return SEC_E_CONTEXT_EXPIRED;
        return SEC_E_OK;
    case REC_ALERT:
        record->DataSize = 0;
        if (plain_size != 2 || (plain[0] != TLS1_ALERT_WARNING && plain[0] != TLS1_ALERT_FATAL))
            return SEC_E_INVALID_TOKEN;
        status = alert_status(plain[0], plain[1]);
        if (status == SEC_I_CONTEXT_EXPIRED)
            t->ReceivedClose = true;
        /* schannel ends the context after too many warnings. */
        if (plain[0] == TLS1_ALERT_WARNING && status == SEC_E_OK && ++t->Warnings > 5)
            status = SEC_I_CONTEXT_EXPIRED;
        return status;
    case REC_HANDSHAKE:
        record->DataSize = 0;
        if (!plain_size)
            return SEC_E_INVALID_TOKEN;
        if (!is13(t)) {
            /* a HelloRequest: mime does not renegotiate, so it is ignored as 5246 7.4.1.1 allows. */
            if (plain_size == 4 && plain[0] == HS_HELLO_REQUEST && !plain[1] && !plain[2] && !plain[3])
                return SEC_E_OK;
            return SEC_E_INVALID_TOKEN;
        }
        if (!TlsBufPut(&t->HandshakeIn, plain, plain_size))
            return SEC_E_INSUFFICIENT_MEMORY;
        status = post_handshake_messages(t);
        if (status)
            return status;
        /*
         * like schannel, the record becomes a plain handshake record left
         * for InitializeSecurityContext in the extra buffer.
         */
        input[0] = REC_HANDSHAKE;
        input[3] = (BYTE)(plain_size >> 8);
        input[4] = (BYTE)plain_size;
        t->PendingSkip = total;
        record->Total = 0;
        return SEC_I_RENEGOTIATE;
    default:
        return SEC_E_INVALID_TOKEN;
    }
}

SECURITY_STATUS TlsEncrypt(TLS *t, BYTE *header, BYTE *data, SIZE_T size, BYTE *trailer, SIZE_T trailer_room,
                           SIZE_T *trailer_used)
{
    if (t->State != ST_CONNECTED)
        return t->State == ST_FAILED ? t->Error : SEC_E_INVALID_HANDLE;
    if (t->SentClose)
        return SEC_E_CONTEXT_EXPIRED;
    SIZE_T tail = seal_trailer_size(t);
    if (size > TLS_MAX_PLAIN || trailer_room < tail)
        return SEC_E_ENCRYPT_FAILURE;
    seal(t, REC_DATA, header, data, size, trailer);
    *trailer_used = tail;
    /* schannel appends the answer to a KeyUpdate request when the trailer has room. */
    if (t->KeyUpdatePending && trailer_room >= tail + 5 + 5 + 17) {
        BYTE *h = trailer + tail, *m = h + 5;
        m[0] = HS_KEY_UPDATE;
        m[1] = 0;
        m[2] = 0;
        m[3] = 1;
        m[4] = 0;
        seal(t, REC_HANDSHAKE, h, m, 5, m + 5);
        key_update(t, &t->Write, t->ClientTraffic);
        t->KeyUpdatePending = false;
        *trailer_used += 5 + 5 + 17;
    }
    return SEC_E_OK;
}

/* ------------------------------------------------------------------ attributes */

void TlsStreamSizes(const TLS *t, SCH_STREAM_SIZES *sizes)
{
    sizes->cbHeader = (ULONG)seal_header_size(t);
    /* tls 1.3 keeps room for a KeyUpdate record after the data, as schannel does. */
    sizes->cbTrailer = is13(t) ? 44 : 16;
    sizes->cbMaximumMessage = TLS_MAX_PLAIN;
    sizes->cBuffers = 4;
    sizes->cbBlockSize = 16;
}

static ULONG protocol_bit(const TLS *t)
{
    return is13(t) ? SP_PROT_TLS1_3_CLIENT : SP_PROT_TLS1_2_CLIENT;
}

static ULONG group_bits(USHORT group)
{
    return group == GROUP_X25519 ? 255 : group == GROUP_P384 ? 384 : 256;
}

void TlsConnectionInfo(const TLS *t, SCH_CONNECTION_INFO *info)
{
    memset(info, 0, sizeof(*info));
    info->dwProtocol = protocol_bit(t);
    info->aiCipher = t->Suite->KeySize == 16 ? 0x660E : 0x6610;
    info->dwCipherStrength = t->Suite->KeySize * 8;
    /* gcm suites have no separate mac. */
    info->aiHash = 0;
    info->dwHashStrength = 0;
    info->aiExch = 0xAE06;
    info->dwExchStrength = group_bits(t->PeerGroup);
}

static void copy_name(WCHAR *out, PCWSTR text)
{
    SIZE_T n = wcslen(text);
    if (n >= SCH_ALG_MAX)
        n = SCH_ALG_MAX - 1;
    memcpy(out, text, n * sizeof(WCHAR));
    out[n] = 0;
}

void TlsCipherInfo(const TLS *t, SCH_CIPHER_INFO *info)
{
    memset(info, 0, sizeof(*info));
    info->dwVersion = 1;
    info->dwProtocol = t->Version;
    info->dwCipherSuite = t->Suite->Id;
    info->dwBaseCipherSuite = t->Suite->Id;
    copy_name(info->szCipherSuite, t->Suite->Name);
    copy_name(info->szCipher, L"AES");
    info->dwCipherLen = t->Suite->KeySize * 8;
    info->dwCipherBlockLen = 16;
    copy_name(info->szHash, t->Suite->Hash == &mime_sha256 ? L"SHA256" : L"SHA384");
    info->dwHashLen = t->Suite->Hash->digest_size * 8;
    copy_name(info->szExchange, L"ECDH");
    info->dwMinExchangeLen = group_bits(t->PeerGroup);
    info->dwMaxExchangeLen = group_bits(t->PeerGroup);
    copy_name(info->szCertificate, t->Key.Type == 2 ? L"ECDSA" : L"RSA");
    info->dwKeyType = t->PeerGroup;
}

void TlsApplicationProtocol(const TLS *t, SCH_APPLICATION_PROTOCOL *protocol)
{
    memset(protocol, 0, sizeof(*protocol));
    if (!t->AlpnSize)
        return;
    protocol->ProtoNegoExt = 2;
    if (t->ProtocolSize) {
        protocol->ProtoNegoStatus = 1;
        protocol->ProtocolIdSize = t->ProtocolSize;
        memcpy(protocol->ProtocolId, t->Protocol, t->ProtocolSize);
    }
}

void TlsSessionInfo(const TLS *t, SCH_SESSION_INFO *info)
{
    memset(info, 0, sizeof(*info));
    info->cbSessionId = t->ServerSessionIdSize;
    memcpy(info->rgbSessionId, t->ServerSessionId, t->ServerSessionIdSize);
}

static PVOID copy_string(PCWSTR text, bool ansi)
{
    SIZE_T n = wcslen(text) + 1;
    BYTE *out = LocalAlloc(LMEM_FIXED, n * (ansi ? 1 : sizeof(WCHAR)));
    if (!out)
        return nullptr;
    for (SIZE_T i = 0; i < n; i++) {
        if (ansi)
            out[i] = (BYTE)text[i];
        else
            ((WCHAR *)out)[i] = text[i];
    }
    return out;
}

SECURITY_STATUS TlsKeyInfo(const TLS *t, SCH_KEY_INFO_W *info, bool ansi)
{
    bool ecdsa = t->Key.Type == 2;
    info->sSignatureAlgorithmName = copy_string(ecdsa ? L"ECDSA" : L"RSA", ansi);
    info->sEncryptAlgorithmName = copy_string(t->Suite->KeySize == 16 ? L"AES-128" : L"AES-256", ansi);
    if (!info->sSignatureAlgorithmName || !info->sEncryptAlgorithmName) {
        LocalFree(info->sSignatureAlgorithmName);
        LocalFree(info->sEncryptAlgorithmName);
        return SEC_E_INSUFFICIENT_MEMORY;
    }
    info->KeySize = t->Suite->KeySize * 8;
    info->SignatureAlgorithm = ecdsa ? CALG_ECDSA : CALG_RSA_SIGN;
    info->EncryptAlgorithm = t->Suite->KeySize == 16 ? 0x660E : 0x6610;
    return SEC_E_OK;
}

PCCERT_CONTEXT TlsRemoteCertificate(const TLS *t)
{
    return t->Certs.Leaf ? SchDuplicate(t->Certs.Leaf) : nullptr;
}

void TlsExpiry(const TLS *t, PTimeStamp expiry)
{
    if (t->Certs.Leaf) {
        FILETIME end = t->Certs.Leaf->pCertInfo->NotAfter;
        expiry->QuadPart = (LONGLONG)((ULONGLONG)end.dwHighDateTime << 32 | end.dwLowDateTime);
    } else {
        expiry->QuadPart = SEC_NEVER_EXPIRES;
    }
}

ULONG TlsReturnedFlags(const TLS *t)
{
    ULONG kept = ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_MANUAL_CRED_VALIDATION | ISC_REQ_INTEGRITY;
    ULONG flags = ISC_RET_REPLAY_DETECT | ISC_RET_SEQUENCE_DETECT | ISC_RET_CONFIDENTIALITY | ISC_RET_STREAM;
    flags |= t->Requested & kept;
    if (t->Requested & ISC_REQ_USE_SUPPLIED_CREDS)
        flags |= ISC_RET_USED_SUPPLIED_CREDS;
    return flags;
}
