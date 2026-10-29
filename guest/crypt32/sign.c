/*
 * CryptSignMessage: a PKCS #7 signed message, attached or detached, signed with the private key of a certificate that
 * a legacy provider holds. the signer always has authenticated attributes, the content type and the message digest
 * after those the caller gives, and the signature is made over their set encoding.
 */
#include "crypt32_private.h"

#define MESSAGE_LIMIT (16 * 1024 * 1024)

static const char data_oid[] = "1.2.840.113549.1.7.1";
static const char signed_data_oid[] = "1.2.840.113549.1.7.2";
static const char content_type_oid[] = "1.2.840.113549.1.9.3";
static const char message_digest_oid[] = "1.2.840.113549.1.9.4";
static const char rsa_oid[] = "1.2.840.113549.1.1.1";

static void write_algorithm(der_writer *w, LPCSTR oid)
{
    DWORD mark = DerBegin(w, ASN_SEQUENCE);
    DerWriteOid(w, oid);
    DerWriteTlv(w, ASN_NULL, nullptr, 0);
    DerEnd(w, mark);
}

static void write_attribute(der_writer *w, LPCSTR oid, DWORD count, const CRYPT_ATTR_BLOB *values)
{
    DWORD mark = DerBegin(w, ASN_SEQUENCE);
    DerWriteOid(w, oid);
    DWORD set = DerBegin(w, ASN_SET);
    for (DWORD i = 0; i < count; i++)
        DerWriteBytes(w, values[i].pbData, values[i].cbData);
    DerEnd(w, set);
    DerEnd(w, mark);
}

static void write_attributes(der_writer *w, DWORD count, const CRYPT_ATTRIBUTE *attributes)
{
    for (DWORD i = 0; i < count; i++)
        write_attribute(w, attributes[i].pszObjId, attributes[i].cValue, attributes[i].rgValue);
}

/* the content of the signed attributes: the caller's, then the content type and the digest of the content. */
static void write_signed_attributes(der_writer *w, const CRYPT_SIGN_MESSAGE_PARA *para, const BYTE *digest,
                                    DWORD digest_size)
{
    write_attributes(w, para->cAuthAttr, para->rgAuthAttr);
    der_writer type;
    DerWriteInit(&type);
    DerWriteOid(&type, data_oid);
    CRYPT_ATTR_BLOB value = {type.size, type.data};
    write_attribute(w, content_type_oid, 1, &value);
    DerWriteFree(&type);
    der_writer hash;
    DerWriteInit(&hash);
    DerWriteTlv(&hash, ASN_OCTETSTRING, digest, digest_size);
    value.cbData = hash.size;
    value.pbData = hash.data;
    write_attribute(w, message_digest_oid, 1, &value);
    DerWriteFree(&hash);
}

BOOL WINAPI CryptSignMessage(CRYPT_SIGN_MESSAGE_PARA *para, BOOL detached, DWORD count, const BYTE *blobs[],
                             DWORD sizes[], BYTE *signed_blob, DWORD *signed_size)
{
    if (!signed_size)
        return CrFail(E_INVALIDARG);
    DWORD capacity = signed_blob ? *signed_size : 0;
    *signed_size = 0;
    if (!para || para->cbSize < offsetof(CRYPT_SIGN_MESSAGE_PARA, dwFlags) || !para->pSigningCert ||
        !(para->dwMsgEncodingType & PKCS_7_ASN_ENCODING) || (count && (!blobs || !sizes)) || (!detached && count != 1) ||
        !para->HashAlgorithm.pszObjId)
        return CrFail(E_INVALIDARG);
    if (para->cMsgCrl)
        return CrFail(ERROR_NOT_SUPPORTED);
    DWORD flags = 0;
    if (para->cbSize >= sizeof(*para)) {
        if (para->dwInnerContentType)
            return CrFail(ERROR_NOT_SUPPORTED);
        flags = para->dwFlags;
    }
    ALG_ID alg = CrHashFromOid(para->HashAlgorithm.pszObjId);
    if (!alg)
        return CrFail(CRYPT_E_UNKNOWN_ALGO);

    /* the pieces are one content to the signature. */
    DWORD total = 0;
    for (DWORD i = 0; i < count; i++) {
        if (!blobs[i] && sizes[i])
            return CrFail(E_INVALIDARG);
        if (sizes[i] > MESSAGE_LIMIT - total)
            return CrFail(CRYPT_E_ASN1_LARGE);
        total += sizes[i];
    }
    BYTE *content = CrAlloc(total ? total : 1);
    if (!content)
        return CrFail(ERROR_OUTOFMEMORY);
    for (DWORD i = 0, at = 0; i < count; at += sizes[i++])
        if (sizes[i])
            memcpy(content + at, blobs[i], sizes[i]);

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD key_spec = 0;
    BOOL release = FALSE;
    DWORD acquire = (flags & CRYPT_MESSAGE_SILENT_KEYSET_FLAG) ? CRYPT_ACQUIRE_SILENT_FLAG : 0;
    if (!CryptAcquireCertificatePrivateKey(para->pSigningCert, acquire, nullptr, &key, &key_spec, &release)) {
        CrFree(content);
        return FALSE;
    }
    if (key_spec == CERT_NCRYPT_KEY_SPEC) {
        /* the keys of key storage providers are not signed with. */
        if (release)
            CrReleaseProvider((HCRYPTPROV)key);
        CrFree(content);
        return CrFail(ERROR_NOT_SUPPORTED);
    }

    BYTE digest[CR_MAX_HASH];
    CrHash(alg, content, total, digest);
    DWORD digest_size = CrHashSize(alg);
    der_writer attributes;
    DerWriteInit(&attributes);
    write_signed_attributes(&attributes, para, digest, digest_size);

    /* the signature is over the attributes as a set. */
    der_writer set;
    DerWriteInit(&set);
    DWORD mark = DerBegin(&set, ASN_SET);
    DerWriteBytes(&set, attributes.data, attributes.size);
    DerEnd(&set, mark);

    BYTE signature[1024];
    DWORD signature_size = sizeof(signature);
    BOOL ok = !set.error && !attributes.error &&
              CrSignData((HCRYPTPROV)key, key_spec, alg, set.data, set.size, signature, &signature_size);
    DWORD error = GetLastError();
    if (release)
        CrReleaseProvider((HCRYPTPROV)key);
    DerWriteFree(&set);
    CrFree(content);
    if (!ok) {
        DerWriteFree(&attributes);
        return CrFail(error ? error : CRYPT_E_MSG_ERROR);
    }
    for (DWORD i = 0; i < signature_size / 2; i++) {
        BYTE swap = signature[i];
        signature[i] = signature[signature_size - 1 - i];
        signature[signature_size - 1 - i] = swap;
    }

    der_writer w;
    DerWriteInit(&w);
    DWORD outer = 0, wrapper = 0;
    if (!(flags & CRYPT_MESSAGE_BARE_CONTENT_OUT_FLAG)) {
        outer = DerBegin(&w, ASN_SEQUENCE);
        DerWriteOid(&w, signed_data_oid);
        wrapper = DerBegin(&w, ASN_CONSTRUCTED_CONTEXT(0));
    }
    DWORD body = DerBegin(&w, ASN_SEQUENCE);
    DerWriteInt(&w, 1);
    DWORD algorithms = DerBegin(&w, ASN_SET);
    write_algorithm(&w, para->HashAlgorithm.pszObjId);
    DerEnd(&w, algorithms);
    DWORD inner = DerBegin(&w, ASN_SEQUENCE);
    DerWriteOid(&w, data_oid);
    if (!detached) {
        DWORD explicit_content = DerBegin(&w, ASN_CONSTRUCTED_CONTEXT(0));
        DerWriteTlv(&w, ASN_OCTETSTRING, count ? blobs[0] : nullptr, count ? sizes[0] : 0);
        DerEnd(&w, explicit_content);
    }
    DerEnd(&w, inner);
    if (para->cMsgCert) {
        DWORD certificates = DerBegin(&w, ASN_CONSTRUCTED_CONTEXT(0));
        for (DWORD i = 0; i < para->cMsgCert; i++)
            DerWriteBytes(&w, para->rgpMsgCert[i]->pbCertEncoded, para->rgpMsgCert[i]->cbCertEncoded);
        DerEnd(&w, certificates);
    }
    DWORD signers = DerBegin(&w, ASN_SET);
    DWORD signer = DerBegin(&w, ASN_SEQUENCE);
    DerWriteInt(&w, 1);
    DWORD identity = DerBegin(&w, ASN_SEQUENCE);
    DerWriteBytes(&w, para->pSigningCert->pCertInfo->Issuer.pbData, para->pSigningCert->pCertInfo->Issuer.cbData);
    DerWriteIntLe(&w, para->pSigningCert->pCertInfo->SerialNumber.pbData,
                  para->pSigningCert->pCertInfo->SerialNumber.cbData, FALSE);
    DerEnd(&w, identity);
    write_algorithm(&w, para->HashAlgorithm.pszObjId);
    DWORD authenticated = DerBegin(&w, ASN_CONSTRUCTED_CONTEXT(0));
    DerWriteBytes(&w, attributes.data, attributes.size);
    DerEnd(&w, authenticated);
    write_algorithm(&w, rsa_oid);
    DerWriteTlv(&w, ASN_OCTETSTRING, signature, signature_size);
    if (para->cUnauthAttr) {
        DWORD unauthenticated = DerBegin(&w, ASN_CONSTRUCTED_CONTEXT(1));
        write_attributes(&w, para->cUnauthAttr, para->rgUnauthAttr);
        DerEnd(&w, unauthenticated);
    }
    DerEnd(&w, signer);
    DerEnd(&w, signers);
    DerEnd(&w, body);
    if (!(flags & CRYPT_MESSAGE_BARE_CONTENT_OUT_FLAG)) {
        DerEnd(&w, wrapper);
        DerEnd(&w, outer);
    }
    DerWriteFree(&attributes);
    if (w.error) {
        DerWriteFree(&w);
        return CrFail(ERROR_OUTOFMEMORY);
    }
    *signed_size = w.size;
    BOOL result = TRUE;
    if (signed_blob) {
        if (capacity < w.size) {
            SetLastError(ERROR_MORE_DATA);
            result = FALSE;
        } else {
            memcpy(signed_blob, w.data, w.size);
        }
    }
    DerWriteFree(&w);
    return result;
}
