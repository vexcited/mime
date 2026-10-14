#include "crypt32_private.h"

#define MESSAGE_LIMIT (16 * 1024 * 1024)
#define MESSAGE_ITEMS 256
#define CRYPT_E_INVALID_INDEX ((DWORD)0x80091008)

typedef struct {
    BYTE *bytes;
    der_item encoded, bare, content, certificates, crls, signers;
    char content_oid[256];
    DWORD version, certificate_count, crl_count, signer_count;
} MESSAGE;

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
        return NULL;
    DWORD start = (pack->used + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
    if (start > MESSAGE_LIMIT || size > MESSAGE_LIMIT - start) {
        pack->error = CRYPT_E_ASN1_LARGE;
        return NULL;
    }
    pack->used = start + size;
    return pack->base ? pack->base + start : NULL;
}

static void blob(PACK *pack, CRYPT_DATA_BLOB *out, const BYTE *data, DWORD size, BOOL reverse)
{
    if (pack->error)
        return;
    BYTE *copy = size ? reserve(pack, size) : NULL;
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
        return NULL;
    char text[256];
    if (oid.length > 64 || DerParseOid(&oid, text, sizeof(text))) {
        pack->error = CRYPT_E_ASN1_CORRUPT;
        return NULL;
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
    der_item oid, parameters = {0};
    if (DerExpect(&input, ASN_OID, &oid) || (input.n && DerRead(&input, &parameters)) || input.n) {
        pack->error = CRYPT_E_ASN1_CORRUPT;
        return;
    }
    char *name = oid_string(pack, oid);
    if (out)
        out->pszObjId = name;
    blob(pack, out ? &out->Parameters : NULL, parameters.start, parameters.total, FALSE);
}

static void attributes(PACK *pack, der_item item, CRYPT_ATTRIBUTES *out)
{
    if (pack->error)
        return;
    DWORD count;
    if ((pack->error = item_count(item, &count)))
        return;
    CRYPT_ATTRIBUTE *array = count ? reserve(pack, count * sizeof(*array)) : NULL;
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
        CRYPT_ATTR_BLOB *blobs = n ? reserve(pack, n * sizeof(*blobs)) : NULL;
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
            blob(pack, blobs ? &blobs[j] : NULL, value.start, value.total, FALSE);
        }
    }
}

static DWORD signer_info(der_item signer, DWORD type, PACK *pack)
{
    if (signer.tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader input = {signer.content, signer.length};
    der_item version, sid, issuer, serial, hash, auth = {0}, encryption, encrypted, unauth = {0};
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
        blob(pack, out ? &out->Issuer : NULL, issuer.start, issuer.total, FALSE);
        blob(pack, out ? &out->SerialNumber : NULL, serial.content, serial.length, TRUE);
    } else {
        CMSG_SIGNER_INFO *out = reserve(pack, sizeof(*out));
        if (out)
            out->dwVersion = number;
        blob(pack, out ? &out->Issuer : NULL, issuer.start, issuer.total, FALSE);
        blob(pack, out ? &out->SerialNumber : NULL, serial.content, serial.length, TRUE);
        algorithm(pack, hash, out ? &out->HashAlgorithm : NULL);
        algorithm(pack, encryption, out ? &out->HashEncryptionAlgorithm : NULL);
        blob(pack, out ? &out->EncryptedHash : NULL, encrypted.content, encrypted.length, FALSE);
        attributes(pack, auth, out ? &out->AuthAttrs : NULL);
        attributes(pack, unauth, out ? &out->UnauthAttrs : NULL);
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
    DerOpen(&input, &outer);
    if (DerExpect(&input, ASN_OID, &oid) || oid.length > 64 || DerParseOid(&oid, name, sizeof(name)) ||
        strcmp(name, "1.2.840.113549.1.7.2") || DerExpect(&input, 0xa0, &wrapper) || input.n)
        return CRYPT_E_INVALID_MSG_TYPE;
    DerOpen(&input, &wrapper);
    if (DerExpect(&input, ASN_SEQUENCE, &message->bare) || input.n)
        return CRYPT_E_ASN1_CORRUPT;
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
        PACK pack = {0};
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
        CrFree(message);
    }
}

static HCRYPTMSG publish(MESSAGE *message)
{
    HCRYPTMSG handle = NULL;
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
        return NULL;
    return messages[index].message;
}

BOOL WINAPI CryptMsgClose(HCRYPTMSG handle)
{
    if (!handle)
        return TRUE;
    AcquireSRWLockExclusive(&message_lock);
    MESSAGE *message = lookup(handle);
    if (message)
        messages[(ULONG_PTR)handle & 255].message = NULL;
    ReleaseSRWLockExclusive(&message_lock);
    if (!message)
        return CrFail(ERROR_INVALID_HANDLE);
    free_message(message);
    return TRUE;
}

static DWORD get_parameter(MESSAGE *message, DWORD type, DWORD index, BYTE *out, DWORD *size)
{
    const void *source = NULL;
    DWORD length = 0, value = 0;
    der_item item = {0};
    BOOL packed = type >= CMSG_SIGNER_INFO_PARAM && type <= CMSG_SIGNER_UNAUTH_ATTR_PARAM;
    if (packed || type == CMSG_ENCODED_SIGNER) {
        if (index >= message->signer_count)
            return CRYPT_E_INVALID_INDEX;
        DWORD error = item_at(message->signers, index, &item);
        if (error)
            return error;
        if (packed) {
            PACK pack = {0};
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

static DWORD read_object(DWORD type, const void *object, BYTE **bytes, DWORD *size)
{
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
    HANDLE file = CreateFileW(object, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    LARGE_INTEGER length;
    DWORD error = 0, read;
    if (!GetFileSizeEx(file, &length))
        error = GetLastError();
    else if (length.QuadPart <= 0 || length.QuadPart > 256 * 1024 * 1024)
        error = ERROR_FILE_TOO_LARGE;
    else if (!(*bytes = CrAlloc((SIZE_T)length.QuadPart)))
        error = ERROR_OUTOFMEMORY;
    else if (!ReadFile(file, *bytes, (DWORD)length.QuadPart, &read, NULL))
        error = GetLastError();
    else if (read != length.QuadPart)
        error = ERROR_READ_FAULT;
    else
        *size = read;
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

BOOL WINAPI CryptQueryObject(DWORD object_type, const void *object, DWORD content_flags, DWORD format_flags,
                              DWORD flags, DWORD *encoding, DWORD *content_type, DWORD *format_type,
                              HCERTSTORE *store_out, HCRYPTMSG *message_out, const void **context)
{
    if (store_out) *store_out = NULL;
    if (message_out) *message_out = NULL;
    if (context) *context = NULL;
    if (encoding) *encoding = 0;
    if (content_type) *content_type = 0;
    if (format_type) *format_type = 0;
    if (flags || !object || !content_flags || !format_flags)
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!(format_flags & CERT_QUERY_FORMAT_FLAG_BINARY))
        return CrFail(CRYPT_E_NO_MATCH);
    BYTE *bytes = NULL;
    DWORD size = 0;
    DWORD error = read_object(object_type, object, &bytes, &size);
    DWORD type = CERT_QUERY_CONTENT_PKCS7_SIGNED, offset = 0, length = size;
    if (!error && size >= 2 && word(bytes) == 0x5a4d) {
        type = CERT_QUERY_CONTENT_PKCS7_SIGNED_EMBED;
        error = embedded(bytes, size, &offset, &length);
    }
    if (!error && (!(content_flags & (1u << type)) || length > MESSAGE_LIMIT))
        error = CRYPT_E_NO_MATCH;
    MESSAGE *message = NULL;
    if (!error) {
        message = CrAllocZero(sizeof(*message));
        if (!message || !(message->bytes = CrAlloc(length)))
            error = ERROR_OUTOFMEMORY;
        else {
            memcpy(message->bytes, bytes + offset, length);
            error = parse_message(message, length);
        }
    }
    CrFree(bytes);
    HCERTSTORE store = NULL;
    if (!error && store_out) {
        store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
        if (!store)
            error = GetLastError();
        for (DWORD i = 0; !error && i < message->certificate_count; i++) {
            der_item cert;
            error = item_at(message->certificates, i, &cert);
            if (!error && (cert.tag != ASN_SEQUENCE ||
                !CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, cert.start, cert.total,
                                                  CERT_STORE_ADD_ALWAYS, NULL)))
                error = CRYPT_E_ASN1_CORRUPT;
        }
        /* the current store has no crl insertion support. */
        if (!error && message->crl_count)
            error = ERROR_NOT_SUPPORTED;
    }
    HCRYPTMSG handle = NULL;
    if (!error && message_out) {
        handle = publish(message);
        if (!handle)
            error = ERROR_OUTOFMEMORY;
        else
            message = NULL;
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
