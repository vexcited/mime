#include "trust_private.h"

static constexpr BYTE oid_signed[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,7,2};
static constexpr BYTE oid_indirect[] = {0x2b,6,1,4,1,0x82,0x37,2,1,4};
static constexpr BYTE oid_pe[] = {0x2b,6,1,4,1,0x82,0x37,2,1,15};
static constexpr BYTE oid_cab[] = {0x2b,6,1,4,1,0x82,0x37,2,1,25};
static constexpr BYTE oid_cab_enhanced[] = {0x2b,6,1,4,1,0x82,0x37,2,1,28};
static constexpr BYTE oid_sip_info[] = {0x2b,6,1,4,1,0x82,0x37,2,1,30};
static constexpr BYTE oid_counter[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,9,6};
static constexpr BYTE oid_time[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,9,5};
static constexpr BYTE oid_rsa_sha1[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,1,5};
static constexpr BYTE oid_rsa_sha256[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,1,11};
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

static LONG file_digest(WT_DER content, const WT_SUBJECT *subject)
{
    WT_BYTES input = content.value;
    WT_DER data, digest, alg, hash;
    if (!WtTake(&input, 0x30, &data) || !WtTake(&input, 0x30, &digest) || input.size)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    BOOL cabinet = subject->digest_input != nullptr;
    /* the second cabinet content format includes additional digests in its link; these are not checked. */
    WT_BYTES probe = data.value;
    if (subject->installer ? !take_oid(&probe, oid_sip_info, sizeof(oid_sip_info))
        : cabinet ? !take_oid(&probe, oid_cab, sizeof(oid_cab)) &&
                      !(probe = data.value, take_oid(&probe, oid_cab_enhanced, sizeof(oid_cab_enhanced)))
                : !take_oid(&data.value, oid_pe, sizeof(oid_pe)))
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (!WtTake(&digest.value, 0x30, &alg) || !WtTake(&digest.value, 4, &hash) || digest.value.size)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    ALG_ID id = algorithm(alg);
    if (!id)
        return NTE_BAD_ALGID;
    if (cabinet)
        return hash_matches(id, (WT_BYTES){subject->digest_input, subject->digest_size}, hash.value) ? 0 :
               TRUST_E_BAD_DIGEST;
    DWORD checksum = subject->checksum, directory = subject->directory, certificate = subject->certificate;
    const BYTE *image = subject->image;
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
    /* crypt32's signed-blob verifier accepts the same RSA digest signature. */
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

/* finds the certificate by issuer and serial number, encoded big-endian without leading zeros. */
static PCCERT_CONTEXT find_cert(HCERTSTORE store, WT_DER issuer, WT_DER serial)
{
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
            return cert;
    }
    return nullptr;
}

/* RSA signatures may identify the key alone or the key and digest together. */
static BOOL rsa_algorithm(WT_DER sequence, ALG_ID alg)
{
    WT_BYTES input = sequence.value;
    WT_DER oid, null;
    if (!WtTake(&input, 6, &oid) || (input.size && (!WtTake(&input, 5, &null) || null.value.size)) || input.size)
        return FALSE;
    return WtOid(oid, oid_rsa, sizeof(oid_rsa)) ||
           (alg == CALG_SHA1 && WtOid(oid, oid_rsa_sha1, sizeof(oid_rsa_sha1))) ||
           (alg == CALG_SHA_256 && WtOid(oid, oid_rsa_sha256, sizeof(oid_rsa_sha256)));
}

static BOOL two_digits(const BYTE *text, DWORD *value)
{
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9')
        return FALSE;
    *value = (text[0] - '0') * 10 + (text[1] - '0');
    return TRUE;
}

/* converts UTCTime or GeneralizedTime ending in Z to file time. */
static BOOL asn_time(WT_DER time, FILETIME *out)
{
    const BYTE *t = time.value.data;
    DWORD v[5], size = time.value.size;
    SYSTEMTIME st = {};
    if (time.tag == 0x17 && (size == 13 || size == 11)) {
        DWORD year;
        if (!two_digits(t, &year))
            return FALSE;
        st.wYear = (WORD)(year >= 50 ? 1900 + year : 2000 + year);
        t += 2;
        size -= 2;
    } else if (time.tag == 0x18 && (size == 15 || size == 13)) {
        DWORD high, low;
        if (!two_digits(t, &high) || !two_digits(t + 2, &low))
            return FALSE;
        st.wYear = (WORD)(high * 100 + low);
        t += 4;
        size -= 4;
    } else
        return FALSE;
    /* month, day, hour and minute, then the seconds when there are some, then the Z. */
    DWORD count = size == 11 ? 5 : 4;
    if (size != (count == 5 ? 11 : 9) || t[size - 1] != 'Z')
        return FALSE;
    for (DWORD i = 0; i < count; i++)
        if (!two_digits(t + i * 2, &v[i]))
            return FALSE;
    st.wMonth = (WORD)v[0];
    st.wDay = (WORD)v[1];
    st.wHour = (WORD)v[2];
    st.wMinute = (WORD)v[3];
    st.wSecond = count == 5 ? (WORD)v[4] : 0;
    return SystemTimeToFileTime(&st, out);
}

/*
 * a countersignature timestamps the signer's signature, using its digest as the message digest. verification to a
 * root trusted at that time allows a signing certificate that has since expired.
 */
static BOOL stamp_time(WT_DER attributes, WT_DER stamped, HCERTSTORE store, FILETIME *when)
{
    WT_BYTES attrs = attributes.value;
    while (attrs.size) {
        WT_DER attribute, oid, values, info;
        if (!WtTake(&attrs, 0x30, &attribute) || !WtTake(&attribute.value, 6, &oid) ||
            !WtTake(&attribute.value, 0x31, &values) || attribute.value.size)
            return FALSE;
        if (!WtOid(oid, oid_counter, sizeof(oid_counter)))
            continue;
        WT_BYTES input;
        WT_DER version, sid, issuer, serial, digest_alg, signed_attrs, sign_alg, encrypted, junk;
        if (!WtTake(&values.value, 0x30, &info) || values.value.size)
            return FALSE;
        input = info.value;
        if (!WtTake(&input, 2, &version) || version.value.size != 1 || version.value.data[0] != 1 ||
            !WtTake(&input, 0x30, &sid) || !WtTake(&sid.value, 0x30, &issuer) || !WtTake(&sid.value, 2, &serial) ||
            sid.value.size || !serial.value.size || (serial.value.data[0] & 0x80) ||
            !WtTake(&input, 0x30, &digest_alg) || !WtTake(&input, 0xa0, &signed_attrs) ||
            !WtTake(&input, 0x30, &sign_alg) || !WtTake(&input, 4, &encrypted) ||
            (input.size && !WtTake(&input, 0xa1, &junk)) || input.size)
            return FALSE;
        ALG_ID alg = algorithm(digest_alg);
        if (!alg || !rsa_algorithm(sign_alg, alg))
            return FALSE;
        BOOL have_time = FALSE, have_digest = FALSE;
        WT_BYTES list = signed_attrs.value;
        while (list.size) {
            WT_DER item, item_oid, item_values, value;
            if (!WtTake(&list, 0x30, &item) || !WtTake(&item.value, 6, &item_oid) ||
                !WtTake(&item.value, 0x31, &item_values) || item.value.size)
                return FALSE;
            if (WtOid(item_oid, oid_time, sizeof(oid_time))) {
                if (have_time ||
                    (!WtTake(&item_values.value, 0x17, &value) && !WtTake(&item_values.value, 0x18, &value)) ||
                    item_values.value.size || !asn_time(value, when))
                    return FALSE;
                have_time = TRUE;
            } else if (WtOid(item_oid, oid_digest, sizeof(oid_digest))) {
                if (have_digest || !WtTake(&item_values.value, 4, &value) || item_values.value.size ||
                    !hash_matches(alg, stamped.value, value.value))
                    return FALSE;
                have_digest = TRUE;
            }
        }
        if (!have_time || !have_digest)
            return FALSE;
        PCCERT_CONTEXT cert = find_cert(store, issuer, serial);
        if (!cert)
            return FALSE;
        BOOL ok = signature(signed_attrs, encrypted, alg, cert) == 0;
        if (ok) {
            /* the service's certificate must chain to a trusted root as of the time it stamped. */
            CERT_CHAIN_PARA para = {};
            para.cbSize = sizeof(para);
            PCCERT_CHAIN_CONTEXT chain = nullptr;
            ok = CertGetCertificateChain(nullptr, cert, when, store, &para, 0, nullptr, &chain);
            if (ok) {
                CERT_CHAIN_POLICY_PARA policy = {sizeof(policy)};
                CERT_CHAIN_POLICY_STATUS status = {sizeof(status)};
                ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policy, &status) &&
                     !status.dwError;
                CertFreeCertificateChain(chain);
            }
        }
        CertFreeCertificateContext(cert);
        return ok;
    }
    return FALSE;
}

#define CRYPT_E_NO_REVOCATION_CHECK_ ((LONG)0x80092012)
#define CRYPT_E_REVOCATION_OFFLINE_ ((LONG)0x80092013)
#define WTPF_OFFLINEOK_IND 0x00000400
#define WTPF_OFFLINEOK_COM 0x00000800
#define WTPF_DEFAULT_STATE 0x00023c00

/*
 * software publishing policy decides whether an unreachable revocation server fails verification. the fresh-install
 * default allows this for both publisher kinds.
 */
static BOOL offline_revocation_allowed(void)
{
    DWORD state = WTPF_DEFAULT_STATE, size = sizeof(state);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\WinTrust\\Trust Providers\\Software Publishing",
                 L"State", RRF_RT_REG_DWORD, nullptr, &state, &size);
    return (state & (WTPF_OFFLINEOK_IND | WTPF_OFFLINEOK_COM)) != 0;
}

static LONG signer(WT_DER encoded, WT_DER content, ALG_ID declared, HCERTSTORE store, const WINTRUST_DATA *options,
                   PCCERT_CHAIN_CONTEXT *kept)
{
    WT_BYTES input = encoded.value;
    WT_DER version, sid, issuer, serial, digest_alg, attributes, sign_alg, encrypted, unsigned_attributes = {};
    if (!WtTake(&input, 2, &version) || version.value.size != 1 || version.value.data[0] != 1 ||
        !WtTake(&input, 0x30, &sid) || !WtTake(&sid.value, 0x30, &issuer) ||
        !WtTake(&sid.value, 2, &serial) || sid.value.size || !serial.value.size || (serial.value.data[0] & 0x80) ||
        !WtTake(&input, 0x30, &digest_alg) || !WtTake(&input, 0xa0, &attributes) ||
        !WtTake(&input, 0x30, &sign_alg) || !WtTake(&input, 4, &encrypted) ||
        (input.size && !WtTake(&input, 0xa1, &unsigned_attributes)) || input.size)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    ALG_ID alg = algorithm(digest_alg);
    if (!alg || alg != declared || !rsa_algorithm(sign_alg, alg))
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
    PCCERT_CONTEXT cert = find_cert(store, issuer, serial);
    if (!cert)
        return TRUST_E_SUBJECT_NOT_TRUSTED;
    LONG result = signature(attributes, encrypted, alg, cert);
    if (!result) {
        LPSTR usages[] = {szOID_PKIX_KP_CODE_SIGNING};
        CERT_CHAIN_PARA para = {};
        para.cbSize = sizeof(para);
        para.RequestedUsage.Usage.cUsageIdentifier = 1;
        para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
        /*
         * provider flags only narrow fdwRevocationChecks: no requested check means none, and absent flags mean the
         * whole chain.
         */
        DWORD flags = 0;
        if (options->fdwRevocationChecks == WTD_REVOKE_WHOLECHAIN) {
            if (options->dwProvFlags & WTD_REVOCATION_CHECK_NONE)
                flags = 0;
            else if (options->dwProvFlags & WTD_REVOCATION_CHECK_END_CERT)
                flags = CERT_CHAIN_REVOCATION_CHECK_END_CERT;
            else if (options->dwProvFlags & WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)
                flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
            else
                flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN;
        }
        /* a signature with a valid time stamp is judged as of that time, else as of now. */
        FILETIME stamp;
        BOOL stamped = unsigned_attributes.value.size && stamp_time(unsigned_attributes, encrypted, store, &stamp);
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        if (!CertGetCertificateChain(nullptr, cert, stamped ? &stamp : nullptr, store, &para, flags, nullptr, &chain))
            result = TRUST_E_SUBJECT_NOT_TRUSTED;
        else {
            CERT_CHAIN_POLICY_PARA policy = {sizeof(policy)};
            CERT_CHAIN_POLICY_STATUS status = {sizeof(status)};
            if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE, chain, &policy, &status))
                result = TRUST_E_SUBJECT_NOT_TRUSTED;
            else
                result = status.dwError;
            /* a server that could not be asked about revocation does not make the signature untrustworthy. */
            if ((result == CRYPT_E_REVOCATION_OFFLINE_ || result == CRYPT_E_NO_REVOCATION_CHECK_) &&
                offline_revocation_allowed())
                result = 0;
            /* keeps the certificate chain even when policy rejects verification. */
            if (kept)
                *kept = chain;
            else
                CertFreeCertificateChain(chain);
        }
    }
    CertFreeCertificateContext(cert);
    return result;
}

LONG WtVerifySignedData(WT_BYTES encoded, const WT_SUBJECT *subject, const WINTRUST_DATA *options,
                        PCCERT_CHAIN_CONTEXT *chain)
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
    LONG result = file_digest(content, subject);
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
