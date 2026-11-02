#include "crypt32_private.h"
#include "../lib/compound/compound.h"

#define MESSAGE_LIMIT (16 * 1024 * 1024)
#define MESSAGE_ITEMS 256
#define CRYPT_E_INVALID_INDEX ((DWORD)0x80091008)
#define CRYPT_E_STREAM_MSG_NOT_READY ((DWORD)0x80091010)
#define CRYPT_E_NO_SIGNER ((DWORD)0x8009200E)

typedef struct {
    BYTE *bytes;
    der_item encoded, bare, content, certificates, crls, signers;
    char content_oid[256];
    DWORD version, certificate_count, crl_count, signer_count;
    /* what CryptMsgUpdate collects: the message, then for a detached one the content it signs. */
    BYTE *pending, *outside;
    DWORD pending_size, pending_capacity, outside_size, outside_capacity;
    DWORD state;
    BOOL detached;
    BOOL bare_input; /* the data is the signed data alone, as a message of a type given to open is. */
} MESSAGE;

enum { MESSAGE_READY, MESSAGE_COLLECTING, MESSAGE_CONTENT, MESSAGE_FAILED };

typedef struct {
    BYTE *base;
    DWORD used, error;
} PACK;

static SRWLOCK message_lock;
static struct { MESSAGE *message; USHORT generation; } messages[256];

static DWORD item_count(der_item item, DWORD *count)
{
    der_reader input = {item.content, item.length};
    der_item child;
    *count = 0;
    while (input.n) {
        DWORD error = DerRead(&input, &child);
        if (error)
            return error;
        if (++*count > MESSAGE_ITEMS)
            return CRYPT_E_ASN1_LARGE;
    }
    return 0;
}

static DWORD item_at(der_item item, DWORD index, der_item *out)
{
    der_reader input = {item.content, item.length};
    for (DWORD i = 0; i <= index; i++) {
        DWORD error = DerRead(&input, out);
        if (error)
            return error;
    }
    return 0;
}

static void *reserve(PACK *pack, DWORD size)
{
    if (pack->error)
        return nullptr;
    DWORD start = (pack->used + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
    if (start > MESSAGE_LIMIT || size > MESSAGE_LIMIT - start) {
        pack->error = CRYPT_E_ASN1_LARGE;
        return nullptr;
    }
    pack->used = start + size;
    return pack->base ? pack->base + start : nullptr;
}

static void blob(PACK *pack, CRYPT_DATA_BLOB *out, const BYTE *data, DWORD size, BOOL reverse)
{
    if (pack->error)
        return;
    BYTE *copy = size ? reserve(pack, size) : nullptr;
    if (copy) {
        if (reverse)
            for (DWORD i = 0; i < size; i++)
                copy[i] = data[size - 1 - i];
        else
            memcpy(copy, data, size);
    }
    if (out) {
        out->cbData = size;
        out->pbData = copy;
    }
}

static char *oid_string(PACK *pack, der_item oid)
{
    if (pack->error)
        return nullptr;
    char text[256];
    if (oid.length > 64 || DerParseOid(&oid, text, sizeof(text))) {
        pack->error = CRYPT_E_ASN1_CORRUPT;
        return nullptr;
    }
    DWORD size = (DWORD)strlen(text) + 1;
    char *out = reserve(pack, size);
    if (out)
        memcpy(out, text, size);
    return out;
}

static void algorithm(PACK *pack, der_item item, CRYPT_ALGORITHM_IDENTIFIER *out)
{
    if (pack->error)
        return;
    der_reader input = {item.content, item.length};
    der_item oid, parameters = {};
    if (DerExpect(&input, ASN_OID, &oid) || (input.n && DerRead(&input, &parameters)) || input.n) {
        pack->error = CRYPT_E_ASN1_CORRUPT;
        return;
    }
    char *name = oid_string(pack, oid);
    if (out)
        out->pszObjId = name;
    blob(pack, out ? &out->Parameters : nullptr, parameters.start, parameters.total, FALSE);
}

static void attributes(PACK *pack, der_item item, CRYPT_ATTRIBUTES *out)
{
    if (pack->error)
        return;
    DWORD count;
    if ((pack->error = item_count(item, &count)))
        return;
    CRYPT_ATTRIBUTE *array = count ? reserve(pack, count * sizeof(*array)) : nullptr;
    if (out) {
        out->cAttr = count;
        out->rgAttr = array;
    }
    der_reader input = {item.content, item.length};
    for (DWORD i = 0; i < count && !pack->error; i++) {
        der_item attr, oid, values;
        if (DerExpect(&input, ASN_SEQUENCE, &attr)) {
            pack->error = CRYPT_E_ASN1_CORRUPT;
            break;
        }
        der_reader fields = {attr.content, attr.length};
        if (DerExpect(&fields, ASN_OID, &oid) || DerExpect(&fields, ASN_SET, &values) || fields.n) {
            pack->error = CRYPT_E_ASN1_CORRUPT;
            break;
        }
        char *name = oid_string(pack, oid);
        if (pack->error)
            break;
        DWORD n;
        if ((pack->error = item_count(values, &n)))
            break;
        CRYPT_ATTR_BLOB *blobs = n ? reserve(pack, n * sizeof(*blobs)) : nullptr;
        if (array) {
            array[i].pszObjId = name;
            array[i].cValue = n;
            array[i].rgValue = blobs;
        }
        der_reader reader = {values.content, values.length};
        for (DWORD j = 0; j < n; j++) {
            der_item value;
            if (DerRead(&reader, &value)) {
                pack->error = CRYPT_E_ASN1_CORRUPT;
                break;
            }
            blob(pack, blobs ? &blobs[j] : nullptr, value.start, value.total, FALSE);
        }
    }
}

static DWORD signer_info(der_item signer, DWORD type, PACK *pack)
{
    if (signer.tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader input = {signer.content, signer.length};
    der_item version, sid, issuer, serial, hash, auth = {}, encryption, encrypted, unauth = {};
    int number;
    if (DerExpect(&input, ASN_INTEGER, &version) || DerParseInt(&version, &number) || number != 1 ||
        DerExpect(&input, ASN_SEQUENCE, &sid) || DerExpect(&input, ASN_SEQUENCE, &hash) ||
        (DerPeek(&input, 0xa0) && DerExpect(&input, 0xa0, &auth)) ||
        DerExpect(&input, ASN_SEQUENCE, &encryption) || DerExpect(&input, ASN_OCTETSTRING, &encrypted) ||
        (input.n && DerExpect(&input, 0xa1, &unauth)) || input.n)
        return CRYPT_E_ASN1_CORRUPT;
    der_reader identity = {sid.content, sid.length};
    if (DerExpect(&identity, ASN_SEQUENCE, &issuer) || DerExpect(&identity, ASN_INTEGER, &serial) ||
        identity.n || !serial.length || (serial.content[0] & 0x80))
        return CRYPT_E_ASN1_CORRUPT;
    if (type == CMSG_SIGNER_AUTH_ATTR_PARAM || type == CMSG_SIGNER_UNAUTH_ATTR_PARAM) {
        CRYPT_ATTRIBUTES *out = reserve(pack, sizeof(*out));
        attributes(pack, type == CMSG_SIGNER_AUTH_ATTR_PARAM ? auth : unauth, out);
    } else if (type == CMSG_SIGNER_HASH_ALGORITHM_PARAM) {
        CRYPT_ALGORITHM_IDENTIFIER *out = reserve(pack, sizeof(*out));
        algorithm(pack, hash, out);
    } else if (type == CMSG_SIGNER_CERT_INFO_PARAM) {
        CERT_INFO *out = reserve(pack, sizeof(*out));
        blob(pack, out ? &out->Issuer : nullptr, issuer.start, issuer.total, FALSE);
        blob(pack, out ? &out->SerialNumber : nullptr, serial.content, serial.length, TRUE);
    } else {
        CMSG_SIGNER_INFO *out = reserve(pack, sizeof(*out));
        if (out)
            out->dwVersion = number;
        blob(pack, out ? &out->Issuer : nullptr, issuer.start, issuer.total, FALSE);
        blob(pack, out ? &out->SerialNumber : nullptr, serial.content, serial.length, TRUE);
        algorithm(pack, hash, out ? &out->HashAlgorithm : nullptr);
        algorithm(pack, encryption, out ? &out->HashEncryptionAlgorithm : nullptr);
        blob(pack, out ? &out->EncryptedHash : nullptr, encrypted.content, encrypted.length, FALSE);
        attributes(pack, auth, out ? &out->AuthAttrs : nullptr);
        attributes(pack, unauth, out ? &out->UnauthAttrs : nullptr);
    }
    return pack->error;
}

static DWORD parse_message(MESSAGE *message, DWORD size)
{
    der_reader input = {message->bytes, size};
    der_item outer, oid, wrapper, version, algorithms, content_info, wrapped;
    char name[256];
    if (DerExpect(&input, ASN_SEQUENCE, &outer))
        return CRYPT_E_ASN1_CORRUPT;
    for (DWORD i = 0; i < input.n; i++)
        if (input.p[i])
            return CRYPT_E_ASN1_CORRUPT;
    message->encoded = outer;
    if (message->bare_input) {
        message->bare = outer;
    } else {
        DerOpen(&input, &outer);
        if (DerExpect(&input, ASN_OID, &oid) || oid.length > 64 || DerParseOid(&oid, name, sizeof(name)) ||
            strcmp(name, "1.2.840.113549.1.7.2") || DerExpect(&input, 0xa0, &wrapper) || input.n)
            return CRYPT_E_INVALID_MSG_TYPE;
        DerOpen(&input, &wrapper);
        if (DerExpect(&input, ASN_SEQUENCE, &message->bare) || input.n)
            return CRYPT_E_ASN1_CORRUPT;
    }
    DerOpen(&input, &message->bare);
    int number;
    if (DerExpect(&input, ASN_INTEGER, &version) || DerParseInt(&version, &number) || number < 1 || number > 5 ||
        DerExpect(&input, ASN_SET, &algorithms) || DerExpect(&input, ASN_SEQUENCE, &content_info) ||
        (DerPeek(&input, 0xa0) && DerExpect(&input, 0xa0, &message->certificates)) ||
        (DerPeek(&input, 0xa1) && DerExpect(&input, 0xa1, &message->crls)) ||
        DerExpect(&input, ASN_SET, &message->signers) || input.n)
        return CRYPT_E_ASN1_CORRUPT;
    message->version = number;
    DerOpen(&input, &content_info);
    if (DerExpect(&input, ASN_OID, &oid) || oid.length > 64 ||
        DerParseOid(&oid, message->content_oid, sizeof(message->content_oid)))
        return CRYPT_E_ASN1_CORRUPT;
    if (input.n) {
        if (DerExpect(&input, 0xa0, &wrapped) || input.n)
            return CRYPT_E_ASN1_CORRUPT;
        DerOpen(&input, &wrapped);
        if (DerRead(&input, &message->content) || input.n)
            return CRYPT_E_ASN1_CORRUPT;
    }
    DWORD error = item_count(message->certificates, &message->certificate_count);
    if (!error)
        error = item_count(message->crls, &message->crl_count);
    if (!error)
        error = item_count(message->signers, &message->signer_count);
    for (DWORD i = 0; !error && i < message->signer_count; i++) {
        der_item signer;
        PACK pack = {};
        error = item_at(message->signers, i, &signer);
        if (!error)
            error = signer_info(signer, CMSG_SIGNER_INFO_PARAM, &pack);
    }
    return error;
}

static void free_message(MESSAGE *message)
{
    if (message) {
        CrFree(message->bytes);
        CrFree(message->pending);
        CrFree(message->outside);
        CrFree(message);
    }
}

static HCRYPTMSG publish(MESSAGE *message)
{
    HCRYPTMSG handle = nullptr;
    AcquireSRWLockExclusive(&message_lock);
    for (DWORD i = 0; i < RTL_NUMBER_OF(messages); i++) {
        if (messages[i].message || messages[i].generation == 0xffff)
            continue;
        messages[i].message = message;
        messages[i].generation++;
        handle = (HCRYPTMSG)(ULONG_PTR)(0x4d000000u | (DWORD)messages[i].generation << 8 | i);
        break;
    }
    ReleaseSRWLockExclusive(&message_lock);
    return handle;
}

static MESSAGE *lookup(HCRYPTMSG handle)
{
    ULONG_PTR value = (ULONG_PTR)handle;
    DWORD index = value & 255;
    if ((value >> 24) != 0x4d || messages[index].generation != ((value >> 8) & 0xffff))
        return nullptr;
    return messages[index].message;
}

BOOL WINAPI CryptMsgClose(HCRYPTMSG handle)
{
    if (!handle)
        return TRUE;
    AcquireSRWLockExclusive(&message_lock);
    MESSAGE *message = lookup(handle);
    if (message)
        messages[(ULONG_PTR)handle & 255].message = nullptr;
    ReleaseSRWLockExclusive(&message_lock);
    if (!message)
        return CrFail(ERROR_INVALID_HANDLE);
    free_message(message);
    return TRUE;
}

static DWORD get_parameter(MESSAGE *message, DWORD type, DWORD index, BYTE *out, DWORD *size)
{
    const void *source = nullptr;
    DWORD length = 0, value = 0;
    der_item item = {};
    BOOL packed = type >= CMSG_SIGNER_INFO_PARAM && type <= CMSG_SIGNER_UNAUTH_ATTR_PARAM;
    if (message->state == MESSAGE_FAILED)
        return CRYPT_E_MSG_ERROR;
    if (message->state != MESSAGE_READY)
        return CRYPT_E_STREAM_MSG_NOT_READY;
    if (packed || type == CMSG_ENCODED_SIGNER) {
        if (index >= message->signer_count)
            return CRYPT_E_INVALID_INDEX;
        DWORD error = item_at(message->signers, index, &item);
        if (error)
            return error;
        if (packed) {
            PACK pack = {};
            error = signer_info(item, type, &pack);
            if (error)
                return error;
            length = pack.used;
        } else {
            source = item.start;
            length = item.total;
        }
    } else if (type == CMSG_CERT_PARAM || type == CMSG_CRL_PARAM) {
        if (index >= (type == CMSG_CERT_PARAM ? message->certificate_count : message->crl_count))
            return CRYPT_E_INVALID_INDEX;
        DWORD error = item_at(type == CMSG_CERT_PARAM ? message->certificates : message->crls, index, &item);
        if (error)
            return error;
        source = item.start;
        length = item.total;
    } else {
        if (index)
            return CRYPT_E_INVALID_INDEX;
        switch (type) {
        case CMSG_TYPE_PARAM: value = CMSG_SIGNED; break;
        case CMSG_VERSION_PARAM: value = message->version; break;
        case CMSG_SIGNER_COUNT_PARAM: value = message->signer_count; break;
        case CMSG_CERT_COUNT_PARAM: value = message->certificate_count; break;
        case CMSG_CRL_COUNT_PARAM: value = message->crl_count; break;
        case CMSG_INNER_CONTENT_TYPE_PARAM:
            source = message->content_oid;
            length = (DWORD)strlen(source) + 1;
            break;
        case CMSG_CONTENT_PARAM:
            source = message->content.tag == ASN_OCTETSTRING ? message->content.content : message->content.start;
            length = message->content.tag == ASN_OCTETSTRING ? message->content.length : message->content.total;
            break;
        case CMSG_BARE_CONTENT_PARAM:
            source = message->bare.start;
            length = message->bare.total;
            break;
        case CMSG_ENCODED_MESSAGE:
            source = message->encoded.start;
            length = message->encoded.total;
            break;
        default: return CRYPT_E_INVALID_MSG_TYPE;
        }
        if (!source && type != CMSG_CONTENT_PARAM) {
            source = &value;
            length = sizeof(value);
        }
    }
    DWORD capacity = *size;
    *size = length;
    if (!out)
        return 0;
    if (capacity < length)
        return ERROR_MORE_DATA;
    if (packed) {
        memset(out, 0, length);
        PACK pack = {out};
        return signer_info(item, type, &pack);
    }
    if (length)
        memcpy(out, source, length);
    return 0;
}

BOOL WINAPI CryptMsgGetParam(HCRYPTMSG handle, DWORD type, DWORD index, void *out, DWORD *size)
{
    if (!size)
        return CrFail(ERROR_INVALID_PARAMETER);
    AcquireSRWLockShared(&message_lock);
    MESSAGE *message = lookup(handle);
    DWORD error = message ? get_parameter(message, type, index, out, size) : ERROR_INVALID_HANDLE;
    ReleaseSRWLockShared(&message_lock);
    return error ? CrFail(error) : TRUE;
}

#define CMSG_TRUSTED_SIGNER_FLAG 0x1
#define CMSG_SIGNER_ONLY_FLAG 0x2
#define CMSG_USE_SIGNER_INDEX_FLAG 0x4

/* 0 for an unknown oid. */
static ALG_ID digest_algorithm(const char *oid)
{
    static const struct {
        const char *Oid;
        ALG_ID Alg;
    } known[] = {{"1.3.14.3.2.26", CALG_SHA1}, {"2.16.840.1.101.3.4.2.1", CALG_SHA_256},
                 {"2.16.840.1.101.3.4.2.2", CALG_SHA_384}, {"2.16.840.1.101.3.4.2.3", CALG_SHA_512},
                 {"1.2.840.113549.2.5", CALG_MD5}};
    for (DWORD i = 0; i < RTL_NUMBER_OF(known); i++)
        if (!strcmp(oid, known[i].Oid))
            return known[i].Alg;
    return 0;
}

static DWORD der_header(BYTE *out, BYTE tag, DWORD length)
{
    out[0] = tag;
    if (length < 128) {
        out[1] = (BYTE)length;
        return 2;
    }
    DWORD n = 1;
    while (n < 4 && (length >> (n * 8)))
        n++;
    out[1] = (BYTE)(0x80 | n);
    for (DWORD i = 0; i < n; i++)
        out[2 + i] = (BYTE)(length >> (8 * (n - 1 - i)));
    return 2 + n;
}

/*
 * whether the signer signed the content with the certificate's key: the content digest must match the message
 * digest the signer attests to, and the RSA signature must hold over the attributes. only signers with signed
 * attributes are judged.
 */
static DWORD verify_signature(const MESSAGE *message, der_item signer, PCCERT_CONTEXT cert)
{
    der_reader input = {signer.content, signer.length};
    der_item version, sid, hash, auth, encryption, encrypted;
    if (DerExpect(&input, ASN_INTEGER, &version) || DerExpect(&input, ASN_SEQUENCE, &sid) ||
        DerExpect(&input, ASN_SEQUENCE, &hash) || !DerPeek(&input, 0xa0) || DerExpect(&input, 0xa0, &auth) ||
        DerExpect(&input, ASN_SEQUENCE, &encryption) || DerExpect(&input, ASN_OCTETSTRING, &encrypted))
        return ERROR_NOT_SUPPORTED;
    der_reader fields = {hash.content, hash.length};
    der_item oid;
    char name[128];
    if (DerExpect(&fields, ASN_OID, &oid) || DerParseOid(&oid, name, sizeof(name)))
        return CRYPT_E_ASN1_CORRUPT;
    ALG_ID alg = digest_algorithm(name);
    if (!alg)
        return NTE_BAD_ALGID;
    /* the message digest attribute the signer attests to. */
    der_reader attrs = {auth.content, auth.length};
    const BYTE *attested = nullptr;
    DWORD attested_size = 0;
    while (attrs.n) {
        der_item attribute, id, values, value;
        if (DerExpect(&attrs, ASN_SEQUENCE, &attribute))
            return CRYPT_E_ASN1_CORRUPT;
        der_reader one = {attribute.content, attribute.length};
        if (DerExpect(&one, ASN_OID, &id) || DerExpect(&one, ASN_SET, &values) || DerParseOid(&id, name, sizeof(name)))
            return CRYPT_E_ASN1_CORRUPT;
        if (!strcmp(name, "1.2.840.113549.1.9.4")) {
            der_reader list = {values.content, values.length};
            if (DerExpect(&list, ASN_OCTETSTRING, &value))
                return CRYPT_E_ASN1_CORRUPT;
            attested = value.content;
            attested_size = value.length;
        }
    }
    if (!attested)
        return CRYPT_E_NOT_FOUND;
    BYTE digest[64];
    DWORD digest_size = sizeof(digest);
    if (message->content.start == nullptr)
        return CRYPT_E_NOT_FOUND;
    if (!CryptHashCertificate(0, alg, 0, message->content.content, message->content.length, digest, &digest_size))
        return GetLastError();
    if (digest_size != attested_size || memcmp(digest, attested, digest_size))
        return NTE_BAD_SIGNATURE;
    /* re-encode the signed attributes as a set, wrapped in the signed form crypt32's verifier takes. */
    BYTE identifier[] = {0x30, 0x0d, 6, 9, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 1, 1, 11, 5, 0};
    if (alg == CALG_SHA1)
        identifier[12] = 5;
    else if (alg == CALG_SHA_384)
        identifier[12] = 12;
    else if (alg == CALG_SHA_512)
        identifier[12] = 13;
    else if (alg == CALG_MD5)
        identifier[12] = 4;
    if (encrypted.length > 8192 || auth.total > 262144)
        return CRYPT_E_ASN1_LARGE;
    BYTE *blob = CrAlloc(auth.total + encrypted.length + 64);
    if (!blob)
        return ERROR_OUTOFMEMORY;
    BYTE bits[6];
    DWORD bits_header = der_header(bits, ASN_BITSTRING, encrypted.length + 1);
    DWORD content = auth.total + sizeof(identifier) + bits_header + 1 + encrypted.length;
    DWORD at = der_header(blob, ASN_SEQUENCE, content);
    memcpy(blob + at, auth.start, auth.total);
    blob[at] = ASN_SET;
    at += auth.total;
    memcpy(blob + at, identifier, sizeof(identifier));
    at += sizeof(identifier);
    memcpy(blob + at, bits, bits_header);
    at += bits_header;
    blob[at++] = 0;
    memcpy(blob + at, encrypted.content, encrypted.length);
    at += encrypted.length;
    BOOL ok = CryptVerifyCertificateSignature(0, X509_ASN_ENCODING, blob, at, &cert->pCertInfo->SubjectPublicKeyInfo);
    CrFree(blob);
    return ok ? 0 : (DWORD)NTE_BAD_SIGNATURE;
}

/*
 * finds the signer's certificate by issuer and serial number among the message's certificates and the given stores.
 * unless the flags ask for the signer alone or trust it, the signature is checked with that certificate's key, which
 * says nothing about whether the certificate is trusted.
 */
BOOL WINAPI CryptMsgGetAndVerifySigner(HCRYPTMSG handle, DWORD store_count, HCERTSTORE *stores, DWORD flags,
                                       PCCERT_CONTEXT *signer, DWORD *signer_index)
{
    if (signer)
        *signer = nullptr;
    if ((flags & ~(DWORD)(CMSG_TRUSTED_SIGNER_FLAG | CMSG_SIGNER_ONLY_FLAG | CMSG_USE_SIGNER_INDEX_FLAG)) ||
        (store_count && !stores))
        return CrFail(E_INVALIDARG);
    AcquireSRWLockShared(&message_lock);
    MESSAGE *message = lookup(handle);
    DWORD count = message ? message->signer_count : 0;
    ReleaseSRWLockShared(&message_lock);
    if (!message)
        return CrFail(ERROR_INVALID_HANDLE);
    DWORD first = 0, last = count;
    if (flags & CMSG_USE_SIGNER_INDEX_FLAG) {
        if (!signer_index || *signer_index >= count)
            return CrFail(CRYPT_E_INVALID_INDEX);
        first = *signer_index;
        last = first + 1;
    }
    /* the certificates the message brings, besides those of the stores. */
    HCERTSTORE own = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!own)
        return FALSE;
    DWORD error = CRYPT_E_NOT_FOUND;
    for (DWORD k = 0; k < message->certificate_count; k++) {
        der_item cert;
        if (!item_at(message->certificates, k, &cert))
            CertAddEncodedCertificateToStore(own, X509_ASN_ENCODING, cert.start, cert.total, CERT_STORE_ADD_ALWAYS,
                                             nullptr);
    }
    for (DWORD index = first; index < last; index++) {
        DWORD bytes = 0;
        if (!CryptMsgGetParam(handle, CMSG_SIGNER_CERT_INFO_PARAM, index, nullptr, &bytes)) {
            error = GetLastError();
            continue;
        }
        CERT_INFO *info = CrAlloc(bytes);
        if (!info) {
            error = ERROR_OUTOFMEMORY;
            break;
        }
        if (!CryptMsgGetParam(handle, CMSG_SIGNER_CERT_INFO_PARAM, index, info, &bytes)) {
            error = GetLastError();
            CrFree(info);
            continue;
        }
        PCCERT_CONTEXT found = CertGetSubjectCertificateFromStore(own, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, info);
        for (DWORD at = 0; !found && at < store_count; at++)
            found = CertGetSubjectCertificateFromStore(stores[at], X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, info);
        CrFree(info);
        if (!found) {
            error = CRYPT_E_NOT_FOUND;
            continue;
        }
        if (!(flags & (CMSG_TRUSTED_SIGNER_FLAG | CMSG_SIGNER_ONLY_FLAG))) {
            der_item item;
            DWORD checked = item_at(message->signers, index, &item);
            if (!checked)
                checked = verify_signature(message, item, found);
            if (checked) {
                error = checked;
                CertFreeCertificateContext(found);
                continue;
            }
        }
        CertCloseStore(own, 0);
        if (signer)
            *signer = found;
        else
            CertFreeCertificateContext(found);
        if (signer_index)
            *signer_index = index;
        return TRUE;
    }
    CertCloseStore(own, 0);
    return CrFail(error);
}

/* ------------------------------------------------------------------ decoding a message as it arrives */

static DWORD append(BYTE **buffer, DWORD *size, DWORD *capacity, const BYTE *data, DWORD count)
{
    if (count > MESSAGE_LIMIT - *size)
        return CRYPT_E_ASN1_LARGE;
    if (*size + count > *capacity || !*buffer) {
        DWORD wanted = *size + count;
        if (wanted < 4096)
            wanted = 4096;
        if (wanted < *capacity * 2 && *capacity * 2 <= MESSAGE_LIMIT)
            wanted = *capacity * 2;
        BYTE *grown = CrAlloc(wanted);
        if (!grown)
            return ERROR_OUTOFMEMORY;
        if (*size)
            memcpy(grown, *buffer, *size);
        CrFree(*buffer);
        *buffer = grown;
        *capacity = wanted;
    }
    if (count)
        memcpy(*buffer + *size, data, count);
    *size += count;
    return 0;
}

/*
 * only signed messages that arrive whole are decoded, with the content following the signature when the message is
 * detached. messages that are streamed to callbacks, enveloped or hashed are not.
 */
HCRYPTMSG WINAPI CryptMsgOpenToDecode(DWORD encoding, DWORD flags, DWORD type, HCRYPTPROV_LEGACY provider,
                                      CERT_INFO *recipient, void *stream)
{
    if (stream) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    if ((type != 0 && type != CMSG_SIGNED) || (flags & ~(DWORD)(CMSG_DETACHED_FLAG | 0x8000))) {
        SetLastError(type > 5 ? E_INVALIDARG : ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    MESSAGE *message = CrAllocZero(sizeof(*message));
    if (!message) {
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    message->state = MESSAGE_COLLECTING;
    message->detached = (flags & CMSG_DETACHED_FLAG) != 0;
    /* a given type means the data is the content of the type alone, with no ContentInfo around it. */
    message->bare_input = type == CMSG_SIGNED;
    HCRYPTMSG handle = publish(message);
    if (!handle) {
        free_message(message);
        SetLastError(ERROR_OUTOFMEMORY);
    }
    return handle;
}

static DWORD update(MESSAGE *message, const BYTE *data, DWORD size, BOOL final)
{
    DWORD error;
    switch (message->state) {
    case MESSAGE_COLLECTING:
        error = append(&message->pending, &message->pending_size, &message->pending_capacity, data, size);
        if (error || !final)
            return error;
        if (!message->pending_size)
            return CRYPT_E_ASN1_EOD;
        message->bytes = message->pending;
        message->pending = nullptr;
        error = parse_message(message, message->pending_size);
        message->state = error ? MESSAGE_FAILED : message->detached ? MESSAGE_CONTENT : MESSAGE_READY;
        return error;
    case MESSAGE_CONTENT:
        error = append(&message->outside, &message->outside_size, &message->outside_capacity, data, size);
        if (error || !final)
            return error;
        /* the digest is taken over the content as it arrived. */
        message->content.tag = ASN_OCTETSTRING;
        message->content.content = message->content.start = message->outside;
        message->content.length = message->content.total = message->outside_size;
        message->state = MESSAGE_READY;
        return 0;
    default:
        return CRYPT_E_MSG_ERROR;
    }
}

BOOL WINAPI CryptMsgUpdate(HCRYPTMSG handle, const BYTE *data, DWORD size, BOOL final)
{
    if (!data && size)
        return CrFail(E_INVALIDARG);
    AcquireSRWLockExclusive(&message_lock);
    MESSAGE *message = lookup(handle);
    DWORD error = message ? update(message, data, size, final) : ERROR_INVALID_HANDLE;
    ReleaseSRWLockExclusive(&message_lock);
    return error ? CrFail(error) : TRUE;
}

HCERTSTORE WINAPI CryptGetMessageCertificates(DWORD encoding, HCRYPTPROV_LEGACY provider, DWORD flags,
                                              const BYTE *signed_blob, DWORD size)
{
    CRYPT_DATA_BLOB blob = {size, (BYTE *)signed_blob};
    return CertOpenStore(CERT_STORE_PROV_PKCS7, encoding, provider, flags, &blob);
}

/*
 * the certificate of the signer is asked of the caller's callback, or looked up by issuer and serial number among the
 * message's own, and then the signature is checked with its key.
 */
static BOOL verify_signer(const CRYPT_VERIFY_MESSAGE_PARA *para, DWORD index, HCRYPTMSG handle, PCCERT_CONTEXT *out)
{
    DWORD count = 0, size = sizeof(count);
    if (!CryptMsgGetParam(handle, CMSG_SIGNER_COUNT_PARAM, 0, &count, &size))
        return FALSE;
    if (index >= count)
        return CrFail(CRYPT_E_NO_SIGNER);
    size = 0;
    if (!CryptMsgGetParam(handle, CMSG_SIGNER_CERT_INFO_PARAM, index, nullptr, &size))
        return FALSE;
    CERT_INFO *info = CrAlloc(size);
    if (!info)
        return CrFail(ERROR_OUTOFMEMORY);
    if (!CryptMsgGetParam(handle, CMSG_SIGNER_CERT_INFO_PARAM, index, info, &size)) {
        CrFree(info);
        return FALSE;
    }
    AcquireSRWLockShared(&message_lock);
    MESSAGE *message = lookup(handle);
    HCERTSTORE store = message ? CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr) : nullptr;
    for (DWORD k = 0; store && k < message->certificate_count; k++) {
        der_item cert;
        if (!item_at(message->certificates, k, &cert))
            CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, cert.start, cert.total, CERT_STORE_ADD_ALWAYS,
                                             nullptr);
    }
    ReleaseSRWLockShared(&message_lock);
    if (!store) {
        CrFree(info);
        return CrFail(ERROR_OUTOFMEMORY);
    }
    PCCERT_CONTEXT found = para->pfnGetSignerCertificate ?
                               para->pfnGetSignerCertificate(para->pvGetArg, para->dwMsgAndCertEncodingType, info, store) :
                               CertGetSubjectCertificateFromStore(store, para->dwMsgAndCertEncodingType, info);
    CrFree(info);
    DWORD error = found ? 0 : GetLastError();
    if (found) {
        AcquireSRWLockShared(&message_lock);
        message = lookup(handle);
        der_item signer;
        error = message ? item_at(message->signers, index, &signer) : ERROR_INVALID_HANDLE;
        if (!error)
            error = verify_signature(message, signer, found);
        ReleaseSRWLockShared(&message_lock);
        if (error) {
            CertFreeCertificateContext(found);
            found = nullptr;
        }
    }
    CertCloseStore(store, 0);
    if (!found)
        return CrFail(error ? error : CRYPT_E_NOT_FOUND);
    if (out)
        *out = found;
    else
        CertFreeCertificateContext(found);
    return TRUE;
}

BOOL WINAPI CryptVerifyDetachedMessageSignature(CRYPT_VERIFY_MESSAGE_PARA *para, DWORD signer_index,
                                                const BYTE *signature, DWORD signature_size, DWORD count,
                                                const BYTE *blobs[], DWORD sizes[], PCCERT_CONTEXT *signer)
{
    if (signer)
        *signer = nullptr;
    if (!para || para->cbSize < sizeof(*para) || (count && (!blobs || !sizes)))
        return CrFail(E_INVALIDARG);
    HCRYPTMSG message = CryptMsgOpenToDecode(para->dwMsgAndCertEncodingType, count ? CMSG_DETACHED_FLAG : 0, 0,
                                             para->hCryptProv, nullptr, nullptr);
    if (!message)
        return FALSE;
    BOOL ok = CryptMsgUpdate(message, signature, signature_size, TRUE);
    for (DWORD i = 0; ok && i < count; i++)
        ok = CryptMsgUpdate(message, blobs[i], sizes[i], i + 1 == count);
    DWORD type = 0, size = sizeof(type);
    if (ok && !CryptMsgGetParam(message, CMSG_TYPE_PARAM, 0, &type, &size))
        ok = FALSE;
    if (ok && type != CMSG_SIGNED) {
        SetLastError(CRYPT_E_UNEXPECTED_MSG_TYPE);
        ok = FALSE;
    }
    ok = ok && verify_signer(para, signer_index, message, signer);
    DWORD error = GetLastError();
    CryptMsgClose(message);
    if (!ok)
        SetLastError(error);
    return ok;
}

/* a blob is copied, a file is mapped, which *mapped tells: signed files such as cabinets can be large. */
static DWORD read_object(DWORD type, const void *object, BYTE **bytes, DWORD *size, BOOL *mapped)
{
    *mapped = FALSE;
    if (!object)
        return ERROR_INVALID_PARAMETER;
    if (type == CERT_QUERY_OBJECT_BLOB) {
        const CRYPT_DATA_BLOB *blob = object;
        if (!blob->pbData || !blob->cbData || blob->cbData > MESSAGE_LIMIT)
            return ERROR_INVALID_PARAMETER;
        *size = blob->cbData;
        *bytes = CrAlloc(*size);
        if (!*bytes)
            return ERROR_OUTOFMEMORY;
        memcpy(*bytes, blob->pbData, *size);
        return 0;
    }
    if (type != CERT_QUERY_OBJECT_FILE)
        return ERROR_INVALID_PARAMETER;
    HANDLE file = CreateFileW(object, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    LARGE_INTEGER length;
    DWORD error = 0;
    HANDLE mapping = nullptr;
    if (!GetFileSizeEx(file, &length))
        error = GetLastError();
    else if (length.QuadPart <= 0)
        error = CRYPT_E_NO_MATCH;
    else if (length.QuadPart > 0xFFFFFFFF)
        error = ERROR_FILE_TOO_LARGE;
    else if (!(mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) ||
             !(*bytes = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)))
        error = GetLastError();
    else {
        *size = (DWORD)length.QuadPart;
        *mapped = TRUE;
    }
    if (mapping)
        CloseHandle(mapping);
    CloseHandle(file);
    return error;
}

static WORD word(const BYTE *p)
{
    return p[0] | (WORD)p[1] << 8;
}

static DWORD dword(const BYTE *p)
{
    return word(p) | (DWORD)word(p + 2) << 16;
}

static DWORD embedded(const BYTE *bytes, DWORD size, DWORD *offset, DWORD *length)
{
    if (size < 64)
        return CRYPT_E_NO_MATCH;
    DWORD nt = dword(bytes + 60);
    if (nt > size - 24 || dword(bytes + nt) != 0x4550)
        return CRYPT_E_NO_MATCH;
    DWORD optional = nt + 24, optional_size = word(bytes + nt + 20);
    if (optional_size > size - optional || optional_size < 2)
        return CRYPT_E_NO_MATCH;
    WORD magic = word(bytes + optional);
    DWORD directories = magic == 0x10b ? 96 : magic == 0x20b ? 112 : 0;
    if (!directories || optional_size < directories + 40 || dword(bytes + optional + directories - 4) <= 4)
        return CRYPT_E_NO_MATCH;
    DWORD at = dword(bytes + optional + directories + 32), total = dword(bytes + optional + directories + 36);
    if (!at || (at & 7) || at > size || total > size - at || total < 8)
        return CRYPT_E_NO_MATCH;
    DWORD n = dword(bytes + at);
    if (n < 8 || n > total || n - 8 > MESSAGE_LIMIT || word(bytes + at + 4) != 0x200 || word(bytes + at + 6) != 2)
        return CRYPT_E_NO_MATCH;
    *offset = at + 8;
    *length = n - 8;
    return 0;
}

/* a cabinet keeps its signature after its data, found through the description in the reserved space of its header. */
static DWORD embedded_cabinet(const BYTE *bytes, DWORD size, DWORD *offset, DWORD *length)
{
    if (size < 44 || dword(bytes) != 0x4643534d || !(word(bytes + 30) & 4))
        return CRYPT_E_NO_MATCH;
    DWORD reserve = word(bytes + 36), cabinet = dword(bytes + 8);
    if (reserve < 4 || reserve > size - 40)
        return CRYPT_E_NO_MATCH;
    const BYTE *area = bytes + 40;
    DWORD before = word(area), description = word(area + 2);
    if (description != 16 || before + 4 + description > reserve)
        return CRYPT_E_NO_MATCH;
    DWORD at = dword(area + 4 + before), n = dword(area + 8 + before);
    if (!n || at != cabinet || at >= size || n != size - at)
        return CRYPT_E_NO_MATCH;
    *offset = at;
    *length = n;
    return 0;
}

static void *compound_allocate(size_t size)
{
    return CrAlloc(size);
}

static void compound_release(void *memory)
{
    CrFree(memory);
}

/* an installer package keeps its signature in the stream named with character 5 then DigitalSignature. */
static DWORD embedded_package(const BYTE *bytes, DWORD size, BYTE **signature, DWORD *length)
{
    static const BYTE magic[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
    static const WCHAR name[] = L"\x05" L"DigitalSignature";
    if (size < 512 || memcmp(bytes, magic, sizeof(magic)))
        return CRYPT_E_NO_MATCH;
    MIME_CFB file;
    if (!MimeCfbOpen(&file, bytes, size, compound_allocate, compound_release))
        return CRYPT_E_NO_MATCH;
    DWORD error = CRYPT_E_NO_MATCH;
    for (uint32_t index = 0; index < file.DirectorySize / 128; index++) {
        MIME_CFB_ENTRY entry;
        if (!MimeCfbEntry(&file, index, &entry) || entry.Type != 2 ||
            memcmp(entry.Name, name, sizeof(name) - sizeof(WCHAR)) || entry.Name[RTL_NUMBER_OF(name) - 1])
            continue;
        if (!entry.Size || entry.Size > MESSAGE_LIMIT)
            break;
        BYTE *copy = CrAlloc((SIZE_T)entry.Size);
        if (!copy) {
            error = ERROR_OUTOFMEMORY;
            break;
        }
        if (!MimeCfbRead(&file, &entry, copy, (size_t)entry.Size)) {
            CrFree(copy);
            break;
        }
        *signature = copy;
        *length = (DWORD)entry.Size;
        error = 0;
        break;
    }
    MimeCfbClose(&file);
    return error;
}

BOOL WINAPI CryptQueryObject(DWORD object_type, const void *object, DWORD content_flags, DWORD format_flags,
                              DWORD flags, DWORD *encoding, DWORD *content_type, DWORD *format_type,
                              HCERTSTORE *store_out, HCRYPTMSG *message_out, const void **context)
{
    if (store_out) *store_out = nullptr;
    if (message_out) *message_out = nullptr;
    if (context) *context = nullptr;
    if (encoding) *encoding = 0;
    if (content_type) *content_type = 0;
    if (format_type) *format_type = 0;
    if (flags || !object || !content_flags || !format_flags)
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!(format_flags & CERT_QUERY_FORMAT_FLAG_BINARY))
        return CrFail(CRYPT_E_NO_MATCH);
    BYTE *bytes = nullptr;
    DWORD size = 0;
    BOOL mapped;
    DWORD error = read_object(object_type, object, &bytes, &size, &mapped);
    DWORD type = CERT_QUERY_CONTENT_PKCS7_SIGNED, offset = 0, length = size;
    if (!error && size >= 2 && word(bytes) == 0x5a4d) {
        type = CERT_QUERY_CONTENT_PKCS7_SIGNED_EMBED;
        error = embedded(bytes, size, &offset, &length);
    } else if (!error && size >= 4 && dword(bytes) == 0x4643534d) {
        type = CERT_QUERY_CONTENT_PKCS7_SIGNED_EMBED;
        error = embedded_cabinet(bytes, size, &offset, &length);
    } else if (!error && size >= 8 && (content_flags & (1u << CERT_QUERY_CONTENT_PKCS7_SIGNED_EMBED))) {
        BYTE *signature = nullptr;
        if (!embedded_package(bytes, size, &signature, &length)) {
            type = CERT_QUERY_CONTENT_PKCS7_SIGNED_EMBED;
            if (mapped)
                UnmapViewOfFile(bytes);
            else
                CrFree(bytes);
            mapped = FALSE;
            bytes = signature;
            size = length;
            offset = 0;
        }
    }
    if (!error && (!(content_flags & (1u << type)) || length > MESSAGE_LIMIT))
        error = CRYPT_E_NO_MATCH;
    MESSAGE *message = nullptr;
    if (!error) {
        message = CrAllocZero(sizeof(*message));
        if (!message || !(message->bytes = CrAlloc(length)))
            error = ERROR_OUTOFMEMORY;
        else {
            memcpy(message->bytes, bytes + offset, length);
            error = parse_message(message, length);
        }
    }
    if (mapped)
        UnmapViewOfFile(bytes);
    else
        CrFree(bytes);
    HCERTSTORE store = nullptr;
    if (!error && store_out) {
        store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
        if (!store)
            error = GetLastError();
        for (DWORD i = 0; !error && i < message->certificate_count; i++) {
            der_item cert;
            error = item_at(message->certificates, i, &cert);
            if (!error && (cert.tag != ASN_SEQUENCE ||
                !CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, cert.start, cert.total,
                                                  CERT_STORE_ADD_ALWAYS, nullptr)))
                error = CRYPT_E_ASN1_CORRUPT;
        }
        /* the current store has no CRL insertion support. */
        if (!error && message->crl_count)
            error = ERROR_NOT_SUPPORTED;
    }
    HCRYPTMSG handle = nullptr;
    if (!error && message_out) {
        handle = publish(message);
        if (!handle)
            error = ERROR_OUTOFMEMORY;
        else
            message = nullptr;
    }
    free_message(message);
    if (error) {
        if (store)
            CertCloseStore(store, 0);
        return CrFail(error);
    }
    if (encoding) *encoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    if (content_type) *content_type = type;
    if (format_type) *format_type = CERT_QUERY_FORMAT_BINARY;
    if (store_out) *store_out = store;
    if (message_out) *message_out = handle;
    return TRUE;
}
