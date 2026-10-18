#include "trust_private.h"

static constexpr BYTE oid_signed[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,7,2};
static constexpr BYTE oid_indirect[] = {0x2b,6,1,4,1,0x82,0x37,2,1,4};
static constexpr BYTE oid_pe[] = {0x2b,6,1,4,1,0x82,0x37,2,1,15};
static constexpr BYTE oid_sha256[] = {0x60,0x86,0x48,1,0x65,3,4,2,1};
static constexpr BYTE oid_sha1[] = {0x2b,0x0e,3,2,0x1a};
static constexpr BYTE oid_rsa[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,1,1};
static constexpr BYTE oid_type[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,9,3};
static constexpr BYTE oid_digest[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,9,4};

BOOL WtTake(WT_BYTES *input, BYTE tag, WT_DER *item)
{
    if (input->size < 2 || input->data[0] != tag)
        return FALSE;
    DWORD header = 2, size = input->data[1];
    if (size & 0x80) {
        DWORD n = size & 0x7f;
        if (!n || n > 4 || n > input->size - 2 || !input->data[2])
            return FALSE;
        size = 0;
        for (DWORD i = 0; i < n; i++)
            size = (size << 8) | input->data[2 + i];
        if (size < 128)
            return FALSE;
        header += n;
    }
    if (size > input->size - header)
        return FALSE;
    *item = (WT_DER){{input->data, header + size}, {input->data + header, size}, tag};
    input->data += header + size;
    input->size -= header + size;
    return TRUE;
}

BOOL WtOid(WT_DER item, const BYTE *value, DWORD size)
{
    return item.tag == 6 && item.value.size == size && !memcmp(item.value.data, value, size);
}

static BOOL take_oid(WT_BYTES *input, const BYTE *value, DWORD size)
{
    WT_DER oid;
    return WtTake(input, 6, &oid) && WtOid(oid, value, size);
}

static ALG_ID algorithm(WT_DER sequence)
{
    WT_BYTES input = sequence.value;
    WT_DER oid, null;
    if (!WtTake(&input, 6, &oid) || (input.size && (!WtTake(&input, 5, &null) || null.value.size)) || input.size)
        return 0;
    if (WtOid(oid, oid_sha256, sizeof(oid_sha256)))
        return CALG_SHA_256;
    if (WtOid(oid, oid_sha1, sizeof(oid_sha1)))
        return CALG_SHA1;
    return 0;
}

static BOOL hash_matches(ALG_ID alg, WT_BYTES data, WT_BYTES expected)
{
    BYTE hash[32];
    DWORD length = sizeof(hash);
    return CryptHashCertificate(0, alg, 0, data.data, data.size, hash, &length) &&
           length == expected.size && !memcmp(hash, expected.data, length);
}

static LONG file_digest(WT_DER content, const BYTE *image, DWORD checksum, DWORD directory, DWORD certificate)
{
    WT_BYTES input = content.value;
    WT_DER data, digest, alg, hash;
    if (!WtTake(&input, 0x30, &data) || !WtTake(&input, 0x30, &digest) || input.size ||
        !take_oid(&data.value, oid_pe, sizeof(oid_pe)) ||
        !WtTake(&digest.value, 0x30, &alg) || !WtTake(&digest.value, 4, &hash) || digest.value.size)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    ALG_ID id = algorithm(alg);
    if (!id)
        return NTE_BAD_ALGID;
    BYTE *bytes = LocalAlloc(LMEM_FIXED, certificate - 12);
    if (!bytes)
        return E_OUTOFMEMORY;
    memcpy(bytes, image, checksum);
    memcpy(bytes + checksum, image + checksum + 4, directory - checksum - 4);
    memcpy(bytes + directory - 4, image + directory + 8, certificate - directory - 8);
    BOOL ok = hash_matches(id, (WT_BYTES){bytes, certificate - 12}, hash.value);
    LocalFree(bytes);
    return ok ? 0 : TRUST_E_BAD_DIGEST;
}

static DWORD header(BYTE *out, BYTE tag, DWORD length)
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

static LONG signature(WT_DER attributes, WT_DER encrypted, ALG_ID alg, PCCERT_CONTEXT cert)
{
    /* crypt32's signed-blob verifier accepts the same rsa digest signature. */
    BYTE identifier[] = {0x30,0x0d,6,9,0x2a,0x86,0x48,0x86,0xf7,0x0d,1,1,11,5,0};
    if (alg == CALG_SHA1)
        identifier[12] = 5;
    if (encrypted.value.size > 8192 || attributes.encoded.size > 65536)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    BYTE *blob = LocalAlloc(LMEM_FIXED, attributes.encoded.size + encrypted.value.size + 64);
    if (!blob)
        return E_OUTOFMEMORY;
    BYTE temporary[6];
    DWORD bit_header = header(temporary, 3, encrypted.value.size + 1);
    DWORD content_size = attributes.encoded.size + sizeof(identifier) + bit_header + 1 + encrypted.value.size;
    DWORD at = header(blob, 0x30, content_size);
    memcpy(blob + at, attributes.encoded.data, attributes.encoded.size);
    blob[at] = 0x31;
    at += attributes.encoded.size;
    memcpy(blob + at, identifier, sizeof(identifier));
    at += sizeof(identifier);
    memcpy(blob + at, temporary, bit_header);
    at += bit_header;
    blob[at++] = 0;
    memcpy(blob + at, encrypted.value.data, encrypted.value.size);
    at += encrypted.value.size;
    BOOL ok = CryptVerifyCertificateSignature(0, X509_ASN_ENCODING, blob, at, &cert->pCertInfo->SubjectPublicKeyInfo);
    LONG error = ok ? 0 : TRUST_E_CERT_SIGNATURE;
    LocalFree(blob);
    return error;
}

static LONG signer(WT_DER encoded, WT_DER content, ALG_ID declared, HCERTSTORE store, const WINTRUST_DATA *options,
                   PCCERT_CHAIN_CONTEXT *kept)
{
    WT_BYTES input = encoded.value;
    WT_DER version, sid, issuer, serial, digest_alg, attributes, sign_alg, encrypted, tail;
    if (!WtTake(&input, 2, &version) || version.value.size != 1 || version.value.data[0] != 1 ||
        !WtTake(&input, 0x30, &sid) || !WtTake(&sid.value, 0x30, &issuer) ||
        !WtTake(&sid.value, 2, &serial) || sid.value.size || !serial.value.size || (serial.value.data[0] & 0x80) ||
        !WtTake(&input, 0x30, &digest_alg) || !WtTake(&input, 0xa0, &attributes) ||
        !WtTake(&input, 0x30, &sign_alg) || !WtTake(&input, 4, &encrypted) ||
        (input.size && !WtTake(&input, 0xa1, &tail)) || input.size)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    ALG_ID alg = algorithm(digest_alg);
    if (!alg || alg != declared || !take_oid(&sign_alg.value, oid_rsa, sizeof(oid_rsa)) ||
        (sign_alg.value.size && (!WtTake(&sign_alg.value, 5, &tail) || tail.value.size)) || sign_alg.value.size)
        return NTE_BAD_ALGID;
    BOOL have_type = FALSE, have_digest = FALSE;
    WT_BYTES attrs = attributes.value;
    while (attrs.size) {
        WT_DER attribute, oid, values, value;
        if (!WtTake(&attrs, 0x30, &attribute) || !WtTake(&attribute.value, 6, &oid) ||
            !WtTake(&attribute.value, 0x31, &values) || attribute.value.size)
            return TRUST_E_SUBJECT_FORM_UNKNOWN;
        if (WtOid(oid, oid_type, sizeof(oid_type))) {
            if (have_type || !take_oid(&values.value, oid_indirect, sizeof(oid_indirect)) || values.value.size)
                return TRUST_E_SUBJECT_FORM_UNKNOWN;
            have_type = TRUE;
        } else if (WtOid(oid, oid_digest, sizeof(oid_digest))) {
            if (have_digest || !WtTake(&values.value, 4, &value) || values.value.size)
                return TRUST_E_SUBJECT_FORM_UNKNOWN;
            if (!hash_matches(alg, content.value, value.value))
                return TRUST_E_BAD_DIGEST;
            have_digest = TRUE;
        }
    }
    if (!have_type || !have_digest)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    CERT_NAME_BLOB name = {issuer.encoded.size, (BYTE *)issuer.encoded.data};
    while (serial.value.size > 1 && !serial.value.data[0]) {
        serial.value.data++;
        serial.value.size--;
    }
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store, cert))) {
        CRYPT_INTEGER_BLOB number = cert->pCertInfo->SerialNumber;
        while (number.cbData > 1 && !number.pbData[number.cbData - 1])
            number.cbData--;
        if (number.cbData != serial.value.size ||
            !CertCompareCertificateName(X509_ASN_ENCODING, &name, &cert->pCertInfo->Issuer))
            continue;
        DWORD i;
        for (i = 0; i < number.cbData; i++)
            if (number.pbData[i] != serial.value.data[number.cbData - 1 - i])
                break;
        if (i == number.cbData)
            break;
    }
    if (!cert)
        return TRUST_E_SUBJECT_NOT_TRUSTED;
    LONG result = signature(attributes, encrypted, alg, cert);
    if (!result) {
        LPSTR usages[] = {szOID_PKIX_KP_CODE_SIGNING};
        CERT_CHAIN_PARA para = {};
        para.cbSize = sizeof(para);
        para.RequestedUsage.Usage.cUsageIdentifier = 1;
        para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
        DWORD flags = 0;
        if (options->fdwRevocationChecks == WTD_REVOKE_WHOLECHAIN ||
            (options->dwProvFlags & WTD_REVOCATION_CHECK_CHAIN))
            flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN;
        else if (options->dwProvFlags & WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)
            flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
        else if (options->dwProvFlags & WTD_REVOCATION_CHECK_END_CERT)
            flags = CERT_CHAIN_REVOCATION_CHECK_END_CERT;
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        if (!CertGetCertificateChain(nullptr, cert, nullptr, store, &para, flags, nullptr, &chain))
            result = TRUST_E_SUBJECT_NOT_TRUSTED;
        else {
            CERT_CHAIN_POLICY_PARA policy = {sizeof(policy)};
            CERT_CHAIN_POLICY_STATUS status = {sizeof(status)};
            if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE, chain, &policy, &status))
                result = TRUST_E_SUBJECT_NOT_TRUSTED;
            else
                result = status.dwError;
            /* the state of a verification keeps the chain, whatever the policy said. */
            if (kept)
                *kept = chain;
            else
                CertFreeCertificateChain(chain);
        }
    }
    CertFreeCertificateContext(cert);
    return result;
}

LONG WtVerifySignedData(WT_BYTES encoded, const BYTE *image, DWORD size, DWORD checksum, DWORD directory,
                       DWORD certificate, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    WT_DER envelope, wrapper, data, version, algorithms, content_info, content_wrapper, content, certs, signers, one;
    if (!WtTake(&encoded, 0x30, &envelope))
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    /* some signers include zero padding in the certificate's declared length. */
    for (DWORD i = 0; i < encoded.size; i++)
        if (encoded.data[i])
            return TRUST_E_SUBJECT_NOT_TRUSTED;
    if (!take_oid(&envelope.value, oid_signed, sizeof(oid_signed)) ||
        !WtTake(&envelope.value, 0xa0, &wrapper) || envelope.value.size ||
        !WtTake(&wrapper.value, 0x30, &data) || wrapper.value.size ||
        !WtTake(&data.value, 2, &version) || version.value.size != 1 || version.value.data[0] != 1 ||
        !WtTake(&data.value, 0x31, &algorithms) ||
        !WtTake(&data.value, 0x30, &content_info) ||
        !take_oid(&content_info.value, oid_indirect, sizeof(oid_indirect)) ||
        !WtTake(&content_info.value, 0xa0, &content_wrapper) || content_info.value.size ||
        !WtTake(&content_wrapper.value, 0x30, &content) || content_wrapper.value.size ||
        !WtTake(&data.value, 0xa0, &certs) || !WtTake(&data.value, 0x31, &signers) || data.value.size ||
        !WtTake(&signers.value, 0x30, &one) || signers.value.size)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    WT_DER digest_algorithm;
    if (!WtTake(&algorithms.value, 0x30, &digest_algorithm) || algorithms.value.size)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    ALG_ID declared = algorithm(digest_algorithm);
    if (!declared)
        return NTE_BAD_ALGID;
    LONG result = file_digest(content, image, checksum, directory, certificate);
    if (result)
        return result;
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!store)
        return E_OUTOFMEMORY;
    DWORD count = 0;
    while (certs.value.size) {
        WT_DER cert;
        if (++count > 64 || !WtTake(&certs.value, 0x30, &cert) ||
            !CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, cert.encoded.data, cert.encoded.size,
                                              CERT_STORE_ADD_ALWAYS, nullptr)) {
            result = TRUST_E_SUBJECT_FORM_UNKNOWN;
            break;
        }
    }
    if (!result)
        result = signer(one, content, declared, store, options, chain);
    CertCloseStore(store, 0);
    return result;
}
