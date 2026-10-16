/*
 * certificate chains: chain engines, chain building, the chain policies
 * and CertFindChainInStore.
 *
 * there are no trusted roots yet, so a chain that reaches a self signed
 * certificate ends with CERT_TRUST_IS_UNTRUSTED_ROOT unless the engine
 * names that root in its exclusive root store.
 */
#include "crypt32_private.h"

#define ENGINE_MAGIC 0x454E4743
#define MAX_CHAIN 20

typedef struct {
    DWORD magic;
    HCERTSTORE world;
    HCERTSTORE roots;
    DWORD flags;
} cr_engine;

typedef struct {
    LONG refs;
    CERT_CHAIN_CONTEXT context;
    CERT_SIMPLE_CHAIN simple;
    PCERT_SIMPLE_CHAIN chains[1];
    PCERT_CHAIN_ELEMENT pointers[MAX_CHAIN];
    CERT_CHAIN_ELEMENT elements[MAX_CHAIN];
} chain_block;

static chain_block *block_of(PCCERT_CHAIN_CONTEXT chain)
{
    return (chain_block *)((BYTE *)chain - offsetof(chain_block, context));
}

BOOL CrIsSelfIssued(PCCERT_CONTEXT cert)
{
    return CertCompareCertificateName(cert->dwCertEncodingType, &cert->pCertInfo->Subject, &cert->pCertInfo->Issuer);
}

/* checks the enhanced key usage extension of a certificate against the requested usages. */
BOOL CrUsageAllowed(PCCERT_CONTEXT cert, const CERT_USAGE_MATCH *usage)
{
    if (!usage || !usage->Usage.cUsageIdentifier)
        return TRUE;
    const CERT_INFO *info = cert->pCertInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_ENHANCED_KEY_USAGE, info->cExtension, info->rgExtension);
    if (!ext)
        return TRUE;
    DWORD size;
    CERT_ENHKEY_USAGE *have = CrDecodeAlloc(X509_ENHANCED_KEY_USAGE, ext->Value.pbData, ext->Value.cbData, 0, &size);
    if (!have)
        return FALSE;
    DWORD matched = 0;
    BOOL any = FALSE;
    for (DWORD j = 0; j < have->cUsageIdentifier; j++)
        if (!strcmp(have->rgpszUsageIdentifier[j], szOID_ANY_ENHANCED_KEY_USAGE))
            any = TRUE;
    for (DWORD i = 0; i < usage->Usage.cUsageIdentifier; i++)
        for (DWORD j = 0; j < have->cUsageIdentifier; j++)
            if (!strcmp(usage->Usage.rgpszUsageIdentifier[i], have->rgpszUsageIdentifier[j])) {
                matched++;
                break;
            }
    CrFree(have);
    if (any)
        return TRUE;
    return usage->dwType == USAGE_MATCH_TYPE_OR ? matched > 0 : matched == usage->Usage.cUsageIdentifier;
}

/* ------------------------------------------------------------------ engines */

BOOL WINAPI CertCreateCertificateChainEngine(PCERT_CHAIN_ENGINE_CONFIG pConfig, HCERTCHAINENGINE *phChainEngine)
{
    /* the layouts before windows 7, of windows 7 without dwExclusiveFlags, and of windows 8. */
    if (!pConfig || !phChainEngine ||
        (pConfig->cbSize != FIELD_OFFSET(CERT_CHAIN_ENGINE_CONFIG, hExclusiveRoot) &&
         pConfig->cbSize != FIELD_OFFSET(CERT_CHAIN_ENGINE_CONFIG, dwExclusiveFlags) &&
         pConfig->cbSize != sizeof(CERT_CHAIN_ENGINE_CONFIG)))
        return CrFail(E_INVALIDARG);
    cr_engine *engine = CrAllocZero(sizeof(cr_engine));
    if (!engine)
        return CrFail(ERROR_OUTOFMEMORY);
    engine->magic = ENGINE_MAGIC;
    engine->flags = pConfig->dwFlags;
    engine->world = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0, 0, nullptr);
    if (!engine->world) {
        CrFree(engine);
        return FALSE;
    }
    if (pConfig->hRestrictedOther)
        CertAddStoreToCollection(engine->world, pConfig->hRestrictedOther, 0, 0);
    if (pConfig->hRestrictedTrust)
        CertAddStoreToCollection(engine->world, pConfig->hRestrictedTrust, 0, 0);
    for (DWORD i = 0; i < pConfig->cAdditionalStore; i++)
        if (pConfig->rghAdditionalStore && pConfig->rghAdditionalStore[i])
            CertAddStoreToCollection(engine->world, pConfig->rghAdditionalStore[i], 0, 0);
    HCERTSTORE roots = pConfig->hRestrictedRoot;
    if (pConfig->cbSize > FIELD_OFFSET(CERT_CHAIN_ENGINE_CONFIG, hExclusiveRoot) && pConfig->hExclusiveRoot)
        roots = pConfig->hExclusiveRoot;
    if (roots) {
        engine->roots = CertDuplicateStore(roots);
        CertAddStoreToCollection(engine->world, roots, 0, 0);
    }
    *phChainEngine = (HCERTCHAINENGINE)engine;
    return TRUE;
}

static cr_engine *engine_of(HCERTCHAINENGINE handle)
{
    if (handle == HCCE_CURRENT_USER || handle == HCCE_LOCAL_MACHINE || handle == HCCE_SERIAL_LOCAL_MACHINE)
        return nullptr;
    cr_engine *engine = (cr_engine *)handle;
    return engine->magic == ENGINE_MAGIC ? engine : nullptr;
}

void WINAPI CertFreeCertificateChainEngine(HCERTCHAINENGINE hChainEngine)
{
    cr_engine *engine = engine_of(hChainEngine);
    if (!engine)
        return;
    engine->magic = 0;
    CertCloseStore(engine->world, 0);
    if (engine->roots)
        CertCloseStore(engine->roots, 0);
    CrFree(engine);
}

BOOL WINAPI CertResyncCertificateChainEngine(HCERTCHAINENGINE hChainEngine)
{
    return TRUE;
}

/* ------------------------------------------------------------------ building */

static BOOL key_id_of(PCCERT_CONTEXT cert, BYTE *id, DWORD *size)
{
    *size = CR_MAX_HASH;
    return CertGetCertificateContextProperty(cert, CERT_KEY_IDENTIFIER_PROP_ID, id, size);
}

/* the authority key identifier of a certificate, if it has one. */
static BOOL authority_key_id(PCCERT_CONTEXT cert, BYTE *id, DWORD *size)
{
    const CERT_INFO *info = cert->pCertInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER2, info->cExtension, info->rgExtension);
    if (!ext)
        return FALSE;
    DWORD n;
    CERT_AUTHORITY_KEY_ID2_INFO *aki = CrDecodeAlloc(X509_AUTHORITY_KEY_ID2, ext->Value.pbData, ext->Value.cbData, 0, &n);
    if (!aki)
        return FALSE;
    BOOL ok = aki->KeyId.cbData && aki->KeyId.cbData <= CR_MAX_HASH;
    if (ok) {
        memcpy(id, aki->KeyId.pbData, aki->KeyId.cbData);
        *size = aki->KeyId.cbData;
    }
    CrFree(aki);
    return ok;
}

/* unsupported signature algorithms must fail chain verification. */
static DWORD check_signature(PCCERT_CONTEXT subject, PCCERT_CONTEXT issuer)
{
    DWORD error = CrVerifySignedBlob(subject->pbCertEncoded, subject->cbCertEncoded,
                                     &issuer->pCertInfo->SubjectPublicKeyInfo);
    return error;
}

static BOOL in_chain(PCCERT_CONTEXT *certs, DWORD count, PCCERT_CONTEXT cert)
{
    for (DWORD i = 0; i < count; i++)
        if (CrSameCert(certs[i], cert))
            return TRUE;
    return FALSE;
}

/* picks the best issuer of subject in a store: a matching key identifier, a valid signature, a valid time. */
static PCCERT_CONTEXT find_issuer(HCERTSTORE store, PCCERT_CONTEXT subject, PCCERT_CONTEXT *certs, DWORD count,
                                  const FILETIME *time, int *best_score, PCCERT_CONTEXT best, DWORD *info_status)
{
    if (!store)
        return best;
    BYTE want[CR_MAX_HASH];
    DWORD want_size = 0;
    BOOL has_aki = authority_key_id(subject, want, &want_size);
    PCCERT_CONTEXT candidate = nullptr;
    while ((candidate = CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_NAME,
                                                   &subject->pCertInfo->Issuer, candidate))) {
        if (in_chain(certs, count, candidate))
            continue;
        int score = 1;
        DWORD status = CERT_TRUST_HAS_NAME_MATCH_ISSUER;
        if (has_aki) {
            BYTE id[CR_MAX_HASH];
            DWORD size;
            if (key_id_of(candidate, id, &size) && size == want_size && !memcmp(id, want, size)) {
                score += 4;
                status = CERT_TRUST_HAS_KEY_MATCH_ISSUER;
            }
        }
        if (!check_signature(subject, candidate))
            score += 8;
        if (!CertVerifyTimeValidity((LPFILETIME)time, candidate->pCertInfo))
            score += 2;
        if (score > *best_score) {
            if (best)
                CertFreeCertificateContext(best);
            best = CertDuplicateCertificateContext(candidate);
            *best_score = score;
            *info_status = status;
        }
    }
    return best;
}

static BOOL is_trusted_root(cr_engine *engine, PCCERT_CONTEXT root)
{
    if (!engine || !engine->roots)
        return FALSE;
    PCCERT_CONTEXT found = CertFindCertificateInStore(engine->roots, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, root,
                                                      nullptr);
    if (!found)
        return FALSE;
    CertFreeCertificateContext(found);
    return TRUE;
}

static DWORD constraint_errors(PCCERT_CONTEXT cert, DWORD position, DWORD ca_below)
{
    if (!position)
        return 0;
    const CERT_INFO *info = cert->pCertInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_BASIC_CONSTRAINTS2, info->cExtension, info->rgExtension);
    if (!ext)
        return 0;
    DWORD size;
    CERT_BASIC_CONSTRAINTS2_INFO *bc = CrDecodeAlloc(X509_BASIC_CONSTRAINTS2, ext->Value.pbData, ext->Value.cbData,
                                                     0, &size);
    if (!bc)
        return CERT_TRUST_INVALID_EXTENSION | CERT_TRUST_INVALID_BASIC_CONSTRAINTS;
    DWORD error = 0;
    if (!bc->fCA || (bc->fPathLenConstraint && ca_below > bc->dwPathLenConstraint))
        error = CERT_TRUST_INVALID_BASIC_CONSTRAINTS;
    CrFree(bc);
    return error;
}

BOOL WINAPI CertGetCertificateChain(HCERTCHAINENGINE hChainEngine, PCCERT_CONTEXT pCertContext, LPFILETIME pTime,
                                    HCERTSTORE hAdditionalStore, PCERT_CHAIN_PARA pChainPara, DWORD dwFlags,
                                    LPVOID pvReserved, PCCERT_CHAIN_CONTEXT *ppChainContext)
{
    if (ppChainContext)
        *ppChainContext = nullptr;
    if (!pCertContext || !ppChainContext || !pChainPara || pChainPara->cbSize < FIELD_OFFSET(CERT_CHAIN_PARA,
                                                                                               RequestedIssuancePolicy))
        return CrFail(E_INVALIDARG);
    cr_engine *engine = engine_of(hChainEngine);
    if (hChainEngine && hChainEngine != HCCE_LOCAL_MACHINE && hChainEngine != HCCE_SERIAL_LOCAL_MACHINE && !engine)
        return CrFail(E_INVALIDARG);
    FILETIME now;
    if (!pTime) {
        GetSystemTimeAsFileTime(&now);
        pTime = &now;
    }
    chain_block *block = CrAllocZero(sizeof(chain_block));
    if (!block)
        return CrFail(ERROR_OUTOFMEMORY);
    PCCERT_CONTEXT certs[MAX_CHAIN];
    DWORD errors[MAX_CHAIN] = {}, infos[MAX_CHAIN] = {}, chain_error = 0;
    DWORD count = 0;
    certs[count++] = CertDuplicateCertificateContext(pCertContext);
    for (;;) {
        PCCERT_CONTEXT cur = certs[count - 1];
        if (CrIsSelfIssued(cur)) {
            DWORD error = check_signature(cur, cur);
            infos[count - 1] |= CERT_TRUST_HAS_NAME_MATCH_ISSUER | CERT_TRUST_HAS_PREFERRED_ISSUER;
            if (!error)
                infos[count - 1] |= CERT_TRUST_IS_SELF_SIGNED;
            else
                errors[count - 1] |= CERT_TRUST_IS_NOT_SIGNATURE_VALID;
            if (!is_trusted_root(engine, cur))
                errors[count - 1] |= CERT_TRUST_IS_UNTRUSTED_ROOT;
            break;
        }
        if (count == MAX_CHAIN) {
            chain_error |= CERT_TRUST_IS_PARTIAL_CHAIN;
            break;
        }
        int score = 0;
        DWORD status = 0;
        PCCERT_CONTEXT issuer = find_issuer(hAdditionalStore, cur, certs, count, pTime, &score, nullptr, &status);
        if (score < 15)
            issuer = find_issuer(cur->hCertStore, cur, certs, count, pTime, &score, issuer, &status);
        if (score < 15 && engine)
            issuer = find_issuer(engine->world, cur, certs, count, pTime, &score, issuer, &status);
        if (!issuer) {
            chain_error |= CERT_TRUST_IS_PARTIAL_CHAIN;
            break;
        }
        infos[count - 1] |= status | CERT_TRUST_HAS_PREFERRED_ISSUER;
        if (check_signature(cur, issuer))
            errors[count - 1] |= CERT_TRUST_IS_NOT_SIGNATURE_VALID;
        certs[count++] = issuer;
    }
    DWORD ca_below = 0;
    BOOL revocation = (dwFlags & (CERT_CHAIN_REVOCATION_CHECK_END_CERT | CERT_CHAIN_REVOCATION_CHECK_CHAIN |
                                  CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)) != 0;
    for (DWORD i = 0; i < count; i++) {
        if (CertVerifyTimeValidity(pTime, certs[i]->pCertInfo))
            errors[i] |= CERT_TRUST_IS_NOT_TIME_VALID;
        errors[i] |= constraint_errors(certs[i], i, ca_below);
        if (i)
            ca_below++;
        if (!CrUsageAllowed(certs[i], &pChainPara->RequestedUsage))
            errors[i] |= CERT_TRUST_IS_NOT_VALID_FOR_USAGE;
        BOOL is_root = i + 1 == count && (infos[i] & CERT_TRUST_IS_SELF_SIGNED);
        if (revocation && (i == 0 || (dwFlags & CERT_CHAIN_REVOCATION_CHECK_CHAIN) ||
                           ((dwFlags & CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT) && !is_root)))
            errors[i] |= CERT_TRUST_REVOCATION_STATUS_UNKNOWN | CERT_TRUST_IS_OFFLINE_REVOCATION;
    }
    DWORD info_all = CERT_TRUST_HAS_PREFERRED_ISSUER;
    for (DWORD i = 0; i < count; i++) {
        CERT_CHAIN_ELEMENT *e = &block->elements[i];
        e->cbSize = sizeof(*e);
        e->pCertContext = certs[i];
        e->TrustStatus.dwErrorStatus = errors[i];
        e->TrustStatus.dwInfoStatus = infos[i];
        block->pointers[i] = e;
        chain_error |= errors[i];
        info_all &= infos[i];
    }
    block->refs = 1;
    block->simple.cbSize = sizeof(block->simple);
    block->simple.TrustStatus.dwErrorStatus = chain_error;
    block->simple.TrustStatus.dwInfoStatus = info_all;
    block->simple.cElement = count;
    block->simple.rgpElement = block->pointers;
    block->chains[0] = &block->simple;
    block->context.cbSize = sizeof(block->context);
    block->context.TrustStatus = block->simple.TrustStatus;
    block->context.cChain = 1;
    block->context.rgpChain = block->chains;
    block->context.dwCreateFlags = dwFlags;
    *ppChainContext = &block->context;
    return TRUE;
}

void WINAPI CertFreeCertificateChain(PCCERT_CHAIN_CONTEXT pChainContext)
{
    if (!pChainContext)
        return;
    chain_block *block = block_of(pChainContext);
    if (InterlockedDecrement(&block->refs))
        return;
    for (DWORD i = 0; i < block->simple.cElement; i++)
        CertFreeCertificateContext(block->elements[i].pCertContext);
    CrFree(block);
}

PCCERT_CHAIN_CONTEXT WINAPI CertDuplicateCertificateChain(PCCERT_CHAIN_CONTEXT pChainContext)
{
    if (!pChainContext)
        return nullptr;
    InterlockedIncrement(&block_of(pChainContext)->refs);
    return pChainContext;
}

/* ------------------------------------------------------------------ policies */

static BOOL first_error(PCCERT_CHAIN_CONTEXT chain, DWORD mask, LONG *chain_index, LONG *element_index)
{
    for (DWORD c = 0; c < chain->cChain; c++) {
        const CERT_SIMPLE_CHAIN *simple = chain->rgpChain[c];
        for (DWORD e = 0; e < simple->cElement; e++)
            if (simple->rgpElement[e]->TrustStatus.dwErrorStatus & mask) {
                *chain_index = (LONG)c;
                *element_index = (LONG)e;
                return TRUE;
            }
        if (simple->TrustStatus.dwErrorStatus & mask) {
            *chain_index = (LONG)c;
            *element_index = simple->cElement ? (LONG)simple->cElement - 1 : 0;
            return TRUE;
        }
    }
    return FALSE;
}

/* the base policy every other one starts from; returns the error found or 0. */
static DWORD base_policy(PCCERT_CHAIN_CONTEXT chain, DWORD flags, LONG *chain_index, LONG *element_index)
{
    static const struct {
        DWORD status;
        DWORD ignore;
        DWORD error;
    } checks[] = {
        {CERT_TRUST_IS_NOT_SIGNATURE_VALID, 0, TRUST_E_CERT_SIGNATURE},
        {CERT_TRUST_IS_REVOKED, 0, CRYPT_E_REVOKED},
        {CERT_TRUST_IS_UNTRUSTED_ROOT, CERT_CHAIN_POLICY_ALLOW_UNKNOWN_CA_FLAG, CERT_E_UNTRUSTEDROOT},
        {CERT_TRUST_IS_PARTIAL_CHAIN, CERT_CHAIN_POLICY_ALLOW_UNKNOWN_CA_FLAG, CERT_E_CHAINING},
        {CERT_TRUST_IS_CYCLIC, 0, CERT_E_CHAINING},
        {CERT_TRUST_IS_NOT_TIME_VALID, CERT_CHAIN_POLICY_IGNORE_NOT_TIME_VALID_FLAG, CERT_E_EXPIRED},
        {CERT_TRUST_IS_NOT_TIME_NESTED, CERT_CHAIN_POLICY_IGNORE_NOT_TIME_NESTED_FLAG, CERT_E_VALIDITYPERIODNESTING},
        {CERT_TRUST_IS_NOT_VALID_FOR_USAGE, CERT_CHAIN_POLICY_IGNORE_WRONG_USAGE_FLAG, CERT_E_WRONG_USAGE},
        {CERT_TRUST_INVALID_BASIC_CONSTRAINTS, CERT_CHAIN_POLICY_IGNORE_INVALID_BASIC_CONSTRAINTS_FLAG,
         TRUST_E_BASIC_CONSTRAINTS},
        {CERT_TRUST_INVALID_EXTENSION, 0, CERT_E_CRITICAL},
        {CERT_TRUST_REVOCATION_STATUS_UNKNOWN, CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS,
         CRYPT_E_REVOCATION_OFFLINE},
    };
    for (DWORD i = 0; i < RTL_NUMBER_OF(checks); i++) {
        if (checks[i].ignore && (flags & checks[i].ignore) == checks[i].ignore)
            continue;
        if (first_error(chain, checks[i].status, chain_index, element_index))
            return checks[i].error;
    }
    return 0;
}

static BOOL dns_matches(LPCWSTR pattern, LPCWSTR host)
{
    if (pattern[0] == '*' && pattern[1] == '.') {
        LPCWSTR dot = wcschr(host, '.');
        return dot && dot != host && !_wcsicmp(pattern + 1, dot);
    }
    return !_wcsicmp(pattern, host);
}

static BOOL server_name_matches(PCCERT_CONTEXT cert, LPCWSTR host)
{
    const CERT_INFO *info = cert->pCertInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_SUBJECT_ALT_NAME2, info->cExtension, info->rgExtension);
    BOOL had_dns = FALSE, found = FALSE;
    if (ext) {
        DWORD size;
        CERT_ALT_NAME_INFO *names = CrDecodeAlloc(X509_ALTERNATE_NAME, ext->Value.pbData, ext->Value.cbData, 0, &size);
        for (DWORD i = 0; names && i < names->cAltEntry && !found; i++)
            if (names->rgAltEntry[i].dwAltNameChoice == CERT_ALT_NAME_DNS_NAME) {
                had_dns = TRUE;
                found = dns_matches(names->rgAltEntry[i].pwszDNSName, host);
            }
        CrFree(names);
    }
    if (found || had_dns)
        return found;
    DWORD n = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, szOID_COMMON_NAME, nullptr, 0);
    WCHAR *cn = CrAlloc(n * sizeof(WCHAR));
    if (!cn)
        return FALSE;
    CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, szOID_COMMON_NAME, cn, n);
    found = cn[0] && dns_matches(cn, host);
    CrFree(cn);
    return found;
}

BOOL WINAPI CertVerifyCertificateChainPolicy(LPCSTR pszPolicyOID, PCCERT_CHAIN_CONTEXT pChainContext,
                                             PCERT_CHAIN_POLICY_PARA pPolicyPara,
                                             PCERT_CHAIN_POLICY_STATUS pPolicyStatus)
{
    if (!pChainContext || !pPolicyStatus || pPolicyStatus->cbSize < FIELD_OFFSET(CERT_CHAIN_POLICY_STATUS,
                                                                                   pvExtraPolicyStatus))
        return CrFail(E_INVALIDARG);
    ULONG_PTR policy = (ULONG_PTR)pszPolicyOID;
    if (!IS_INTRESOURCE(pszPolicyOID) || !policy || policy > 9)
        return CrFail(ERROR_FILE_NOT_FOUND);
    DWORD flags = pPolicyPara ? pPolicyPara->dwFlags : 0;
    LONG chain_index = -1, element_index = -1;
    DWORD error = 0;
    if (policy == 4 || policy == 9) {
        const SSL_EXTRA_CERT_CHAIN_POLICY_PARA *ssl = pPolicyPara ? pPolicyPara->pvExtraPolicyPara : nullptr;
        DWORD checks = ssl ? ssl->fdwChecks : 0;
        if (checks & SECURITY_FLAG_IGNORE_UNKNOWN_CA)
            flags |= CERT_CHAIN_POLICY_ALLOW_UNKNOWN_CA_FLAG;
        if (checks & SECURITY_FLAG_IGNORE_CERT_DATE_INVALID)
            flags |= CERT_CHAIN_POLICY_IGNORE_NOT_TIME_VALID_FLAG;
        if (checks & SECURITY_FLAG_IGNORE_WRONG_USAGE)
            flags |= CERT_CHAIN_POLICY_IGNORE_WRONG_USAGE_FLAG;
        if (checks & SECURITY_FLAG_IGNORE_REVOCATION)
            flags |= CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
        error = base_policy(pChainContext, flags, &chain_index, &element_index);
        if (!error && ssl && ssl->pwszServerName && ssl->pwszServerName[0] &&
            ssl->dwAuthType != AUTHTYPE_CLIENT && !(checks & SECURITY_FLAG_IGNORE_CERT_CN_INVALID) &&
            pChainContext->cChain && pChainContext->rgpChain[0]->cElement &&
            !server_name_matches(pChainContext->rgpChain[0]->rgpElement[0]->pCertContext, ssl->pwszServerName)) {
            error = CERT_E_CN_NO_MATCH;
            chain_index = 0;
            element_index = 0;
        }
    } else if (policy == 5) {
        if (first_error(pChainContext, CERT_TRUST_INVALID_BASIC_CONSTRAINTS, &chain_index, &element_index))
            error = TRUST_E_BASIC_CONSTRAINTS;
    } else {
        error = base_policy(pChainContext, flags, &chain_index, &element_index);
        if (!error && policy == 7) {
            /* no root is a microsoft root, as no root is trusted. */
            error = CERT_E_UNTRUSTEDROOT;
            chain_index = 0;
            element_index = pChainContext->cChain ? (LONG)pChainContext->rgpChain[0]->cElement - 1 : 0;
        }
    }
    pPolicyStatus->dwError = error;
    pPolicyStatus->lChainIndex = error ? chain_index : -1;
    pPolicyStatus->lElementIndex = error ? element_index : -1;
    return TRUE;
}

/* ------------------------------------------------------------------ CertFindChainInStore */

static BOOL chain_has_issuer(PCCERT_CHAIN_CONTEXT chain, const CERT_CHAIN_FIND_BY_ISSUER_PARA *para, DWORD *chain_at,
                             DWORD *element_at)
{
    if (!para->cIssuer)
        return TRUE;
    for (DWORD c = 0; c < chain->cChain; c++) {
        const CERT_SIMPLE_CHAIN *simple = chain->rgpChain[c];
        for (DWORD e = 0; e < simple->cElement; e++) {
            PCCERT_CONTEXT cert = simple->rgpElement[e]->pCertContext;
            for (DWORD i = 0; i < para->cIssuer; i++)
                if (CertCompareCertificateName(X509_ASN_ENCODING, &cert->pCertInfo->Issuer, &para->rgIssuer[i])) {
                    *chain_at = c;
                    *element_at = e;
                    return TRUE;
                }
        }
    }
    return FALSE;
}

PCCERT_CHAIN_CONTEXT WINAPI CertFindChainInStore(HCERTSTORE hCertStore, DWORD dwCertEncodingType, DWORD dwFindFlags,
                                                 DWORD dwFindType, const void *pvFindPara,
                                                 PCCERT_CHAIN_CONTEXT pPrevChainContext)
{
    const CERT_CHAIN_FIND_BY_ISSUER_PARA *para = pvFindPara;
    PCCERT_CONTEXT cert = nullptr;
    if (pPrevChainContext) {
        if (pPrevChainContext->cChain && pPrevChainContext->rgpChain[0]->cElement)
            cert = CertDuplicateCertificateContext(pPrevChainContext->rgpChain[0]->rgpElement[0]->pCertContext);
        CertFreeCertificateChain(pPrevChainContext);
    }
    if (dwFindType != CERT_CHAIN_FIND_BY_ISSUER || !para ||
        para->cbSize < FIELD_OFFSET(CERT_CHAIN_FIND_BY_ISSUER_PARA, pdwIssuerChainIndex)) {
        if (cert)
            CertFreeCertificateContext(cert);
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    while ((cert = CertEnumCertificatesInStore(hCertStore, cert))) {
        if (!(dwFindFlags & CERT_CHAIN_FIND_BY_ISSUER_NO_KEY_FLAG) && !CrHasPrivateKey(cert))
            continue;
        if (para->pszUsageIdentifier) {
            LPSTR usage = (LPSTR)para->pszUsageIdentifier;
            CERT_USAGE_MATCH match = {USAGE_MATCH_TYPE_AND, {1, &usage}};
            if (!CrUsageAllowed(cert, &match))
                continue;
        }
        CERT_CHAIN_PARA chain_para = {sizeof(chain_para)};
        PCCERT_CHAIN_CONTEXT chain;
        HCERTCHAINENGINE engine =
            (dwFindFlags & CERT_CHAIN_FIND_BY_ISSUER_LOCAL_MACHINE_FLAG) ? HCCE_LOCAL_MACHINE : HCCE_CURRENT_USER;
        if (!CertGetCertificateChain(engine, cert, nullptr, nullptr, &chain_para, 0, nullptr, &chain))
            continue;
        DWORD chain_at = 0, element_at = 0;
        if (chain_has_issuer(chain, para, &chain_at, &element_at) &&
            (!para->pfnFindCallback || para->pfnFindCallback(chain, para->pvFindArg))) {
            if (para->cbSize >= sizeof(CERT_CHAIN_FIND_BY_ISSUER_PARA)) {
                if (para->pdwIssuerChainIndex)
                    *para->pdwIssuerChainIndex = chain_at;
                if (para->pdwIssuerElementIndex)
                    *para->pdwIssuerElementIndex = element_at;
            }
            CertFreeCertificateContext(cert);
            return chain;
        }
        CertFreeCertificateChain(chain);
    }
    SetLastError(CRYPT_E_NOT_FOUND);
    return nullptr;
}
