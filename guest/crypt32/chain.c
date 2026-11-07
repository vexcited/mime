/*
 * certificate chains: engines, chain building, policies and CertFindChainInStore.
 *
 * a chain ending in a self signed certificate is trusted when the engine's Root store holds it: the logical Root
 * store of the current user or local machine, plus the roots the host's bundle seeds into the machine's AuthRoot
 * store and those automatic root update brings there, or the engine's exclusive or restricted roots.
 */
#include "crypt32_private.h"

#define ENGINE_MAGIC 0x454E4743
#define MAX_CHAIN 20

typedef struct {
    DWORD magic;
    LONG refs;
    HCERTSTORE world;
    HCERTSTORE roots;
    HCERTSTORE system_roots;
    HCERTSTORE disallowed;
    DWORD flags;
    LONG generation;
    ULONGLONG checked;
} cr_engine;

typedef struct {
    LONG refs;
    CERT_CHAIN_CONTEXT context;
    CERT_SIMPLE_CHAIN simple;
    PCERT_SIMPLE_CHAIN chains[1];
    PCERT_CHAIN_ELEMENT pointers[MAX_CHAIN];
    CERT_CHAIN_ELEMENT elements[MAX_CHAIN];
    CERT_REVOCATION_INFO revocation[MAX_CHAIN];
    CERT_REVOCATION_CRL_INFO crl[MAX_CHAIN];
} chain_block;

static chain_block *block_of(PCCERT_CHAIN_CONTEXT chain)
{
    return (chain_block *)((BYTE *)chain - offsetof(chain_block, context));
}

BOOL CrIsSelfIssued(PCCERT_CONTEXT cert)
{
    return CertCompareCertificateName(cert->dwCertEncodingType, &cert->pCertInfo->Subject, &cert->pCertInfo->Issuer);
}

/* whether an encoded list of usages allows the requested ones. */
static BOOL usages_allow(const BYTE *data, DWORD size, const CERT_USAGE_MATCH *usage)
{
    DWORD n;
    CERT_ENHKEY_USAGE *have = CrDecodeAlloc(X509_ENHANCED_KEY_USAGE, data, size, 0, &n);
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

/* a property holding an encoded list of usages, in a buffer to free with CrFree. */
static BYTE *usage_property(PCCERT_CONTEXT cert, DWORD id, DWORD *size)
{
    *size = 0;
    if (!CertGetCertificateContextProperty(cert, id, nullptr, size) || !*size)
        return nullptr;
    BYTE *data = CrAlloc(*size);
    if (data && !CertGetCertificateContextProperty(cert, id, data, size)) {
        CrFree(data);
        data = nullptr;
    }
    return data;
}

/* checks the enhanced key usage extension of a certificate, and the usages a property limits it to, against the
   requested usages. */
BOOL CrUsageAllowed(PCCERT_CONTEXT cert, const CERT_USAGE_MATCH *usage)
{
    if (!usage || !usage->Usage.cUsageIdentifier)
        return TRUE;
    const CERT_INFO *info = cert->pCertInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_ENHANCED_KEY_USAGE, info->cExtension, info->rgExtension);
    if (ext && !usages_allow(ext->Value.pbData, ext->Value.cbData, usage))
        return FALSE;
    DWORD size;
    BYTE *prop = usage_property(cert, CERT_ENHKEY_USAGE_PROP_ID, &size);
    BOOL ok = !prop || usages_allow(prop, size, usage);
    CrFree(prop);
    return ok;
}

/* ------------------------------------------------------------------ engines */

/*
 * an engine searches its stores for issuers and trusts the roots of its location's Root store, or only its
 * exclusive or restricted roots when it has them. the Disallowed store marks certificates distrusted. the default
 * engines of the current user and local machine are built once, and rebuilt when the registry under their stores
 * changes.
 */
static HCERTSTORE open_system(DWORD location, PCWSTR name)
{
    return CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                         location | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG, name);
}

static void add_to_world(cr_engine *engine, HCERTSTORE store)
{
    if (store)
        CertAddStoreToCollection(engine->world, store, 0, 0);
}

static cr_engine *new_engine(DWORD flags)
{
    cr_engine *engine = CrAllocZero(sizeof(cr_engine));
    if (!engine) {
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    engine->magic = ENGINE_MAGIC;
    engine->refs = 1;
    engine->flags = flags;
    engine->world = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0, 0, nullptr);
    if (!engine->world) {
        CrFree(engine);
        return nullptr;
    }
    return engine;
}

/* a location's roots, intermediate authorities, and the user's own and trusted stores. */
static void add_system_stores(cr_engine *engine, DWORD location)
{
    engine->generation = CrRegGeneration;
    engine->checked = GetTickCount64();
    engine->disallowed = open_system(location, L"Disallowed");
    engine->system_roots = open_system(location, L"Root");
    add_to_world(engine, engine->system_roots);
    static const PCWSTR others[] = {L"CA", L"My", L"Trust"};
    for (DWORD i = 0; i < RTL_NUMBER_OF(others); i++) {
        HCERTSTORE store = open_system(location, others[i]);
        add_to_world(engine, store);
        if (store)
            CertCloseStore(store, 0);
    }
}

static void release_engine(cr_engine *engine)
{
    if (!engine || InterlockedDecrement(&engine->refs))
        return;
    engine->magic = 0;
    CertCloseStore(engine->world, 0);
    CertCloseStore(engine->roots, 0);
    CertCloseStore(engine->system_roots, 0);
    CertCloseStore(engine->disallowed, 0);
    CrFree(engine);
}

static SRWLOCK default_lock = SRWLOCK_INIT;
static cr_engine *defaults[2];

/* whether a default engine's stores are older than the registry, checked at most once a second. */
static BOOL engine_stale(cr_engine *engine)
{
    if (engine->generation != CrRegGeneration)
        return TRUE;
    ULONGLONG now = GetTickCount64();
    if (now - engine->checked < 1000)
        return FALSE;
    engine->checked = now;
    return CrStoreChanged(CrStore(engine->world)) ||
           (engine->disallowed && CrStoreChanged(CrStore(engine->disallowed)));
}

/* returns a reference; built on first use and again after its stores changed. */
static cr_engine *default_engine(BOOL machine, BOOL rebuild)
{
    AcquireSRWLockExclusive(&default_lock);
    cr_engine *engine = defaults[machine];
    if (engine && (rebuild || engine_stale(engine))) {
        defaults[machine] = nullptr;
        release_engine(engine);
        engine = nullptr;
    }
    if (!engine) {
        engine = new_engine(machine ? CERT_CHAIN_USE_LOCAL_MACHINE_STORE : 0);
        if (engine) {
            add_system_stores(engine, machine ? CERT_SYSTEM_STORE_LOCAL_MACHINE : CERT_SYSTEM_STORE_CURRENT_USER);
            defaults[machine] = engine;
        }
    }
    if (engine)
        InterlockedIncrement(&engine->refs);
    ReleaseSRWLockExclusive(&default_lock);
    return engine;
}

BOOL WINAPI CertCreateCertificateChainEngine(PCERT_CHAIN_ENGINE_CONFIG pConfig, HCERTCHAINENGINE *phChainEngine)
{
    /* the layouts before windows 7, of windows 7 without dwExclusiveFlags, and of windows 8. */
    if (!pConfig || !phChainEngine ||
        (pConfig->cbSize != FIELD_OFFSET(CERT_CHAIN_ENGINE_CONFIG, hExclusiveRoot) &&
         pConfig->cbSize != FIELD_OFFSET(CERT_CHAIN_ENGINE_CONFIG, dwExclusiveFlags) &&
         pConfig->cbSize != sizeof(CERT_CHAIN_ENGINE_CONFIG)))
        return CrFail(E_INVALIDARG);
    cr_engine *engine = new_engine(pConfig->dwFlags);
    if (!engine)
        return FALSE;
    add_to_world(engine, pConfig->hRestrictedOther);
    add_to_world(engine, pConfig->hRestrictedTrust);
    for (DWORD i = 0; i < pConfig->cAdditionalStore; i++)
        if (pConfig->rghAdditionalStore)
            add_to_world(engine, pConfig->rghAdditionalStore[i]);
    HCERTSTORE roots = pConfig->hRestrictedRoot;
    if (pConfig->cbSize > FIELD_OFFSET(CERT_CHAIN_ENGINE_CONFIG, hExclusiveRoot) && pConfig->hExclusiveRoot)
        roots = pConfig->hExclusiveRoot;
    if (roots) {
        engine->roots = CertDuplicateStore(roots);
        add_to_world(engine, roots);
    }
    /* restricted stores keep the engine to what they hold; otherwise it shares the default engine's stores. */
    if (!pConfig->hRestrictedOther && !pConfig->hRestrictedTrust) {
        cr_engine *system = default_engine((pConfig->dwFlags & CERT_CHAIN_USE_LOCAL_MACHINE_STORE) != 0, FALSE);
        if (system) {
            add_to_world(engine, system->world);
            if (!roots && system->system_roots)
                engine->system_roots = CertDuplicateStore(system->system_roots);
            if (system->disallowed)
                engine->disallowed = CertDuplicateStore(system->disallowed);
            release_engine(system);
        }
    }
    *phChainEngine = (HCERTCHAINENGINE)engine;
    return TRUE;
}

static BOOL is_default(HCERTCHAINENGINE handle)
{
    return handle == HCCE_CURRENT_USER || handle == HCCE_LOCAL_MACHINE || handle == HCCE_SERIAL_LOCAL_MACHINE;
}

/* returns a reference, or null when the handle is not an engine. */
static cr_engine *engine_of(HCERTCHAINENGINE handle)
{
    if (is_default(handle))
        return default_engine(handle != HCCE_CURRENT_USER, FALSE);
    cr_engine *engine = (cr_engine *)handle;
    if (engine->magic != ENGINE_MAGIC)
        return nullptr;
    InterlockedIncrement(&engine->refs);
    return engine;
}

void WINAPI CertFreeCertificateChainEngine(HCERTCHAINENGINE hChainEngine)
{
    if (is_default(hChainEngine))
        return;
    cr_engine *engine = (cr_engine *)hChainEngine;
    if (engine->magic == ENGINE_MAGIC)
        release_engine(engine);
}

BOOL WINAPI CertResyncCertificateChainEngine(HCERTCHAINENGINE hChainEngine)
{
    if (is_default(hChainEngine)) {
        release_engine(default_engine(hChainEngine != HCCE_CURRENT_USER, TRUE));
        return TRUE;
    }
    cr_engine *engine = (cr_engine *)hChainEngine;
    if (engine->magic != ENGINE_MAGIC)
        return CrFail(E_INVALIDARG);
    CrResyncStore(CrStore(engine->world));
    if (engine->disallowed)
        CrResyncStore(CrStore(engine->disallowed));
    return TRUE;
}

/* ------------------------------------------------------------------ building */

static BOOL key_id_of(PCCERT_CONTEXT cert, BYTE *id, DWORD *size)
{
    *size = CR_MAX_HASH;
    return CertGetCertificateContextProperty(cert, CERT_KEY_IDENTIFIER_PROP_ID, id, size);
}

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

static BOOL store_has(HCERTSTORE store, PCCERT_CONTEXT cert)
{
    if (!store)
        return FALSE;
    PCCERT_CONTEXT found = CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, cert, nullptr);
    if (!found)
        return FALSE;
    CertFreeCertificateContext(found);
    return TRUE;
}

/* the engine's own roots replace those of its location. */
static BOOL is_trusted_root(cr_engine *engine, PCCERT_CONTEXT root)
{
    if (!engine)
        return FALSE;
    if (engine->roots)
        return store_has(engine->roots, root);
    return store_has(engine->system_roots, root) || store_has(CrAutoRoots(), root);
}

/* picks the best issuer of subject in a store: a trusted root, a valid signature, a matching key id, a valid time. */
static PCCERT_CONTEXT find_issuer(HCERTSTORE store, cr_engine *engine, PCCERT_CONTEXT subject, PCCERT_CONTEXT *certs,
                                  DWORD count, const FILETIME *time, int *best_score, PCCERT_CONTEXT best,
                                  DWORD *info_status)
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
        /* a path ending in a trusted root wins over a cross certificate the server sent. */
        if (CrIsSelfIssued(candidate) && is_trusted_root(engine, candidate))
            score += 16;
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

/* builds the path from the certificate up; returns the number of certificates, each a reference. */
static DWORD build(cr_engine *engine, PCCERT_CONTEXT cert, const FILETIME *time, HCERTSTORE additional,
                   PCCERT_CONTEXT *certs, DWORD *errors, DWORD *infos, DWORD *chain_error)
{
    memset(errors, 0, MAX_CHAIN * sizeof(DWORD));
    memset(infos, 0, MAX_CHAIN * sizeof(DWORD));
    *chain_error = 0;
    DWORD count = 0;
    certs[count++] = CertDuplicateCertificateContext(cert);
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
            *chain_error |= CERT_TRUST_IS_PARTIAL_CHAIN;
            break;
        }
        int score = 0;
        DWORD status = 0;
        PCCERT_CONTEXT issuer = find_issuer(additional, engine, cur, certs, count, time, &score, nullptr, &status);
        if (score < 31)
            issuer = find_issuer(cur->hCertStore, engine, cur, certs, count, time, &score, issuer, &status);
        if (score < 31 && engine)
            issuer = find_issuer(engine->world, engine, cur, certs, count, time, &score, issuer, &status);
        if (score < 31 && engine && !engine->roots)
            issuer = find_issuer(CrAutoRoots(), engine, cur, certs, count, time, &score, issuer, &status);
        if (!issuer) {
            *chain_error |= CERT_TRUST_IS_PARTIAL_CHAIN;
            break;
        }
        infos[count - 1] |= status | CERT_TRUST_HAS_PREFERRED_ISSUER;
        if (check_signature(cur, issuer))
            errors[count - 1] |= CERT_TRUST_IS_NOT_SIGNATURE_VALID;
        certs[count++] = issuer;
    }
    return count;
}

/* ------------------------------------------------------------------ revocation */

#define CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT 0x08000000
#define DEFAULT_URL_TIMEOUT 15000

typedef struct {
    HCERTSTORE crls;
    LPFILETIME time;
    DWORD flags;
    const CERT_CHAIN_PARA *para;
} revocation_ctx;

/*
 * asks the revocation providers about one element, with the next one as its issuer. a root only tells when it is
 * revoked: a root without a list to check is not an error. other answers that are not a success leave the status
 * unknown, and offline unless no check was possible at all.
 */
static void check_revocation(const revocation_ctx *ctx, PCCERT_CONTEXT *certs, DWORD count, DWORD i, BOOL is_root,
                             chain_block *block, DWORD *error)
{
    CERT_REVOCATION_CRL_INFO *crl = &block->crl[i];
    crl->cbSize = sizeof(*crl);
    CERT_REVOCATION_PARA para = {sizeof(para)};
    para.pIssuerCert = i + 1 < count ? certs[i + 1] : is_root ? certs[i] : nullptr;
    para.hCrlStore = ctx->crls;
    para.pftTimeToUse = ctx->time;
    para.dwUrlRetrievalTimeout = ctx->para->dwUrlRetrievalTimeout ? ctx->para->dwUrlRetrievalTimeout
                                                                  : DEFAULT_URL_TIMEOUT;
    para.fCheckFreshnessTime = ctx->para->fCheckRevocationFreshnessTime;
    para.dwFreshnessTime = ctx->para->dwRevocationFreshnessTime;
    para.pCrlInfo = crl;
    para.pftCacheResync = ctx->para->pftCacheResync;
    DWORD flags = 0;
    if (ctx->flags & CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY)
        flags |= CERT_VERIFY_CACHE_ONLY_BASED_REVOCATION;
    if (ctx->flags & CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT)
        flags |= CERT_VERIFY_REV_ACCUMULATIVE_TIMEOUT_FLAG;
    CERT_REVOCATION_STATUS status = {sizeof(status)};
    PVOID context = (PVOID)certs[i];
    DWORD saved = GetLastError();
    CertVerifyRevocation(X509_ASN_ENCODING, CERT_CONTEXT_REVOCATION_TYPE, 1, &context, flags, &para, &status);
    SetLastError(saved);
    DWORD result = status.dwError;
    if (is_root && result != (DWORD)CRYPT_E_REVOKED)
        result = 0;
    if (result == (DWORD)CRYPT_E_REVOKED)
        *error |= CERT_TRUST_IS_REVOKED;
    else if (result) {
        *error |= CERT_TRUST_REVOCATION_STATUS_UNKNOWN;
        if (result != (DWORD)CRYPT_E_NO_REVOCATION_CHECK)
            *error |= CERT_TRUST_IS_OFFLINE_REVOCATION;
    }
    CERT_REVOCATION_INFO *info = &block->revocation[i];
    info->cbSize = sizeof(*info);
    info->dwRevocationResult = result;
    info->fHasFreshnessTime = status.fHasFreshnessTime;
    info->dwFreshnessTime = status.dwFreshnessTime;
    info->pCrlInfo = crl->pBaseCrlContext ? crl : nullptr;
    block->elements[i].pRevocationInfo = info;
}

#define CERT_DISALLOWED_FILETIME_PROP_ID 104
#define CERT_DISALLOWED_ENHKEY_USAGE_PROP_ID 122
#define CERT_ROOT_PROGRAM_NOT_BEFORE_FILETIME_PROP_ID 126
#define CERT_ROOT_PROGRAM_NOT_BEFORE_ENHKEY_USAGE_PROP_ID 127

static BOOL filetime_property(PCCERT_CONTEXT cert, DWORD id, ULONGLONG *out)
{
    DWORD size = sizeof(*out);
    return CertGetCertificateContextProperty(cert, id, out, &size) && size == sizeof(*out);
}

/* whether a property's list of usages names one of those requested; with no list, whether any is requested. */
static BOOL usage_listed(PCCERT_CONTEXT cert, DWORD id, const CERT_USAGE_MATCH *usage, BOOL *present)
{
    DWORD size;
    BYTE *prop = usage_property(cert, id, &size);
    *present = prop != nullptr;
    if (!prop)
        return FALSE;
    CERT_USAGE_MATCH any = {USAGE_MATCH_TYPE_OR, usage->Usage};
    BOOL listed = usage->Usage.cUsageIdentifier && usages_allow(prop, size, &any);
    CrFree(prop);
    return listed;
}

/*
 * what the root program's properties on a root say: it is disabled from a date, it may not be trusted for some
 * usages, and certificates issued after a date are not trusted through it, for some usages or for all.
 */
static DWORD root_program_errors(PCCERT_CONTEXT root, PCCERT_CONTEXT leaf, const CERT_USAGE_MATCH *usage)
{
    ULONGLONG when;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    if (filetime_property(root, CERT_DISALLOWED_FILETIME_PROP_ID, &when) &&
        (!when || ((ULONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime) >= when))
        return CERT_TRUST_IS_UNTRUSTED_ROOT;
    BOOL present;
    if (usage_listed(root, CERT_DISALLOWED_ENHKEY_USAGE_PROP_ID, usage, &present))
        return CERT_TRUST_IS_NOT_VALID_FOR_USAGE;
    const FILETIME *issued = &leaf->pCertInfo->NotBefore;
    if (filetime_property(root, CERT_ROOT_PROGRAM_NOT_BEFORE_FILETIME_PROP_ID, &when) && when &&
        ((ULONGLONG)issued->dwHighDateTime << 32 | issued->dwLowDateTime) >= when &&
        (usage_listed(root, CERT_ROOT_PROGRAM_NOT_BEFORE_ENHKEY_USAGE_PROP_ID, usage, &present) || !present))
        return CERT_TRUST_IS_NOT_VALID_FOR_USAGE;
    return 0;
}

BOOL WINAPI CertGetCertificateChain(HCERTCHAINENGINE hChainEngine, PCCERT_CONTEXT pCertContext, LPFILETIME pTime,
                                    HCERTSTORE hAdditionalStore, PCERT_CHAIN_PARA pChainPara, DWORD dwFlags,
                                    LPVOID pvReserved, PCCERT_CHAIN_CONTEXT *ppChainContext)
{
    if (ppChainContext)
        *ppChainContext = nullptr;
    if (!pCertContext || !ppChainContext || !pChainPara || pvReserved)
        return CrFail(E_INVALIDARG);
    /* the structure grew over versions, so read only as much as cbSize covers. a size of 0 asks for nothing in
       particular. */
    CERT_CHAIN_PARA normalized = {};
    memcpy(&normalized, pChainPara, pChainPara->cbSize < sizeof(normalized) ? pChainPara->cbSize : sizeof(normalized));
    normalized.cbSize = sizeof(normalized);
    pChainPara = &normalized;
    cr_engine *engine = engine_of(hChainEngine);
    if (!engine && !is_default(hChainEngine))
        return CrFail(E_INVALIDARG);
    FILETIME now;
    if (!pTime) {
        GetSystemTimeAsFileTime(&now);
        pTime = &now;
    }
    chain_block *block = CrAllocZero(sizeof(chain_block));
    if (!block) {
        release_engine(engine);
        return CrFail(ERROR_OUTOFMEMORY);
    }
    PCCERT_CONTEXT certs[MAX_CHAIN];
    DWORD errors[MAX_CHAIN], infos[MAX_CHAIN], chain_error;
    DWORD count = build(engine, pCertContext, pTime, hAdditionalStore, certs, errors, infos, &chain_error);
    /* a chain the system roots do not end may end in a root the program trusts. */
    if (engine && !engine->roots && !(dwFlags & CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE) &&
        ((chain_error & CERT_TRUST_IS_PARTIAL_CHAIN) || (errors[count - 1] & CERT_TRUST_IS_UNTRUSTED_ROOT))) {
        PCCERT_CONTEXT top = certs[count - 1];
        BYTE aki[CR_MAX_HASH];
        DWORD aki_size = 0;
        BOOL self = CrIsSelfIssued(top);
        if (self || !authority_key_id(top, aki, &aki_size))
            aki_size = 0;
        if (CrAutoRootUpdate(top, self, aki, aki_size)) {
            for (DWORD i = 0; i < count; i++)
                CertFreeCertificateContext(certs[i]);
            count = build(engine, pCertContext, pTime, hAdditionalStore, certs, errors, infos, &chain_error);
        }
    }
    DWORD ca_below = 0;
    BOOL revocation = (dwFlags & (CERT_CHAIN_REVOCATION_CHECK_END_CERT | CERT_CHAIN_REVOCATION_CHECK_CHAIN |
                                  CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)) != 0;
    revocation_ctx rev = {nullptr, pTime, dwFlags, pChainPara};
    if (revocation) {
        rev.crls = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0, 0, nullptr);
        if (rev.crls && hAdditionalStore)
            CertAddStoreToCollection(rev.crls, hAdditionalStore, 0, 0);
        if (rev.crls && engine)
            CertAddStoreToCollection(rev.crls, engine->world, 0, 0);
    }
    for (DWORD i = 0; i < count; i++) {
        if (CertVerifyTimeValidity(pTime, certs[i]->pCertInfo))
            errors[i] |= CERT_TRUST_IS_NOT_TIME_VALID;
        errors[i] |= constraint_errors(certs[i], i, ca_below);
        if (i)
            ca_below++;
        if (!CrUsageAllowed(certs[i], &pChainPara->RequestedUsage))
            errors[i] |= CERT_TRUST_IS_NOT_VALID_FOR_USAGE;
        if (engine && store_has(engine->disallowed, certs[i]))
            errors[i] |= CERT_TRUST_IS_EXPLICIT_DISTRUST;
        BOOL is_root = i + 1 == count && (infos[i] & CERT_TRUST_IS_SELF_SIGNED);
        if (is_root)
            errors[i] |= root_program_errors(certs[i], certs[0], &pChainPara->RequestedUsage);
        if (revocation && (i == 0 || (dwFlags & CERT_CHAIN_REVOCATION_CHECK_CHAIN) ||
                           ((dwFlags & CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT) && !is_root)))
            check_revocation(&rev, certs, count, i, is_root, block, &errors[i]);
    }
    if (rev.crls)
        CertCloseStore(rev.crls, 0);
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
    release_engine(engine);
    return TRUE;
}

void WINAPI CertFreeCertificateChain(PCCERT_CHAIN_CONTEXT pChainContext)
{
    if (!pChainContext)
        return;
    chain_block *block = block_of(pChainContext);
    if (InterlockedDecrement(&block->refs))
        return;
    for (DWORD i = 0; i < block->simple.cElement; i++) {
        CertFreeCertificateContext(block->elements[i].pCertContext);
        CertFreeCRLContext(block->crl[i].pBaseCrlContext);
        CertFreeCRLContext(block->crl[i].pDeltaCrlContext);
    }
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

/*
 * an unknown revocation status counts unless the flag of its element's place ignores it: the end certificate, a
 * root, or an authority between them. an element that was offline wins over one where no check was possible.
 */
static DWORD revocation_policy(PCCERT_CHAIN_CONTEXT chain, DWORD flags, LONG *chain_index, LONG *element_index)
{
    if ((flags & CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS) == CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS)
        return 0;
    DWORD error = 0;
    for (DWORD c = 0; c < chain->cChain; c++) {
        const CERT_SIMPLE_CHAIN *simple = chain->rgpChain[c];
        for (DWORD e = 0; e < simple->cElement; e++) {
            const CERT_CHAIN_ELEMENT *element = simple->rgpElement[e];
            if (!(element->TrustStatus.dwErrorStatus & CERT_TRUST_REVOCATION_STATUS_UNKNOWN))
                continue;
            DWORD ignore = (element->TrustStatus.dwInfoStatus & CERT_TRUST_IS_SELF_SIGNED)
                               ? CERT_CHAIN_POLICY_IGNORE_ROOT_REV_UNKNOWN_FLAG
                           : e ? CERT_CHAIN_POLICY_IGNORE_CA_REV_UNKNOWN_FLAG
                           : c ? CERT_CHAIN_POLICY_IGNORE_CTL_SIGNER_REV_UNKNOWN_FLAG
                               : CERT_CHAIN_POLICY_IGNORE_END_REV_UNKNOWN_FLAG;
            if (flags & ignore)
                continue;
            DWORD result = element->pRevocationInfo &&
                                   element->pRevocationInfo->dwRevocationResult == (DWORD)CRYPT_E_NO_REVOCATION_CHECK
                               ? (DWORD)CRYPT_E_NO_REVOCATION_CHECK
                               : (DWORD)CRYPT_E_REVOCATION_OFFLINE;
            if (!error || result == (DWORD)CRYPT_E_REVOCATION_OFFLINE) {
                error = result;
                *chain_index = (LONG)c;
                *element_index = (LONG)e;
                if (result == (DWORD)CRYPT_E_REVOCATION_OFFLINE)
                    return error;
            }
        }
    }
    return error;
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
        {CERT_TRUST_IS_EXPLICIT_DISTRUST, 0, CRYPT_E_REVOKED},
        {CERT_TRUST_IS_UNTRUSTED_ROOT, CERT_CHAIN_POLICY_ALLOW_UNKNOWN_CA_FLAG, CERT_E_UNTRUSTEDROOT},
        {CERT_TRUST_IS_PARTIAL_CHAIN, CERT_CHAIN_POLICY_ALLOW_UNKNOWN_CA_FLAG, CERT_E_CHAINING},
        {CERT_TRUST_IS_REVOKED, 0, CRYPT_E_REVOKED},
        {CERT_TRUST_IS_CYCLIC, 0, CERT_E_CHAINING},
        {CERT_TRUST_IS_NOT_TIME_VALID, CERT_CHAIN_POLICY_IGNORE_NOT_TIME_VALID_FLAG, CERT_E_EXPIRED},
        {CERT_TRUST_IS_NOT_TIME_NESTED, CERT_CHAIN_POLICY_IGNORE_NOT_TIME_NESTED_FLAG, CERT_E_VALIDITYPERIODNESTING},
        {CERT_TRUST_IS_NOT_VALID_FOR_USAGE, CERT_CHAIN_POLICY_IGNORE_WRONG_USAGE_FLAG, CERT_E_WRONG_USAGE},
        {CERT_TRUST_INVALID_BASIC_CONSTRAINTS, CERT_CHAIN_POLICY_IGNORE_INVALID_BASIC_CONSTRAINTS_FLAG,
         TRUST_E_BASIC_CONSTRAINTS},
        {CERT_TRUST_INVALID_EXTENSION, 0, CERT_E_CRITICAL},
    };
    for (DWORD i = 0; i < RTL_NUMBER_OF(checks); i++) {
        if (checks[i].ignore && (flags & checks[i].ignore) == checks[i].ignore)
            continue;
        if (first_error(chain, checks[i].status, chain_index, element_index))
            return checks[i].error;
    }
    return revocation_policy(chain, flags, chain_index, element_index);
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

/*
 * whether the chain ends in a Microsoft program root, a certificate the system trusts for its own code, known by its
 * SHA-1 hash. these are the five the Windows root store keeps: Microsoft Root Authority, Microsoft Authenticode Root
 * Authority, Microsoft Root Certificate Authority and its 2010 and 2011 successors. test roots and the program's key
 * lists are not checked.
 */
static BOOL microsoft_root(PCCERT_CHAIN_CONTEXT chain)
{
    static const BYTE roots[][20] = {
        {0xA4, 0x34, 0x89, 0x15, 0x9A, 0x52, 0x0F, 0x0D, 0x93, 0xD0, 0x32, 0xCC, 0xAF, 0x37, 0xE7, 0xFE, 0x20, 0xA8, 0xB4, 0x19},
        {0x7F, 0x88, 0xCD, 0x72, 0x23, 0xF3, 0xC8, 0x13, 0x81, 0x8C, 0x99, 0x46, 0x14, 0xA8, 0x9C, 0x99, 0xFA, 0x3B, 0x52, 0x47},
        {0xCD, 0xD4, 0xEE, 0xAE, 0x60, 0x00, 0xAC, 0x7F, 0x40, 0xC3, 0x80, 0x2C, 0x17, 0x1E, 0x30, 0x14, 0x80, 0x30, 0xC0, 0x72},
        {0x3B, 0x1E, 0xFD, 0x3A, 0x66, 0xEA, 0x28, 0xB1, 0x66, 0x97, 0x39, 0x47, 0x03, 0xA7, 0x2C, 0xA3, 0x40, 0xA0, 0x5B, 0xD5},
        {0x8F, 0x43, 0x28, 0x8A, 0xD2, 0x72, 0xF3, 0x10, 0x3B, 0x6F, 0xB1, 0x42, 0x84, 0x85, 0xEA, 0x30, 0x14, 0xC0, 0xBC, 0xFE},
    };
    if (!chain->cChain || !chain->rgpChain[0]->cElement)
        return FALSE;
    const CERT_SIMPLE_CHAIN *simple = chain->rgpChain[0];
    PCCERT_CONTEXT root = simple->rgpElement[simple->cElement - 1]->pCertContext;
    BYTE hash[20];
    DWORD size = sizeof(hash);
    if (!CryptHashCertificate(0, CALG_SHA1, 0, root->pbCertEncoded, root->cbCertEncoded, hash, &size))
        return FALSE;
    for (DWORD i = 0; i < RTL_NUMBER_OF(roots); i++)
        if (!memcmp(hash, roots[i], sizeof(hash)))
            return TRUE;
    return FALSE;
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
        if (!error && policy == 7 && !microsoft_root(pChainContext)) {
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
