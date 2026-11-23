#include "trust_private.h"

/*
 * the action wininet verifies the certificate of a server with, as the steps of wintrust 26300 run it: the
 * certificate comes in a WINTRUST_CERT_INFO with the stores the server sent, the policy data is the
 * HTTPSPolicyCallbackData the chain policy of crypt32 takes as it is.
 */

/* with the first two flags of the certificate information the chain is built on an engine that trusts nothing. */
static HCERTCHAINENGINE restricted_engine(const WINTRUST_CERT_INFO *info)
{
    if (info->cbStruct <= offsetof(WINTRUST_CERT_INFO, dwFlags) || !(info->dwFlags & 3))
        return nullptr;
    HCERTSTORE empty = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!empty)
        return nullptr;
    CERT_CHAIN_ENGINE_CONFIG config = {.cbSize = sizeof(config), .hRestrictedTrust = empty, .hRestrictedOther = empty};
    if (info->dwFlags & 1)
        config.hRestrictedRoot = empty;
    HCERTCHAINENGINE engine = nullptr;
    if (!CertCreateCertificateChainEngine(&config, &engine))
        engine = nullptr;
    CertCloseStore(empty, 0);
    return engine;
}

static HCERTSTORE additional_store(const WINTRUST_CERT_INFO *info)
{
    if (!info->chStores)
        return nullptr;
    if (info->chStores == 1)
        return CertDuplicateStore(info->pahStores[0]);
    HCERTSTORE all = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0, 0, nullptr);
    for (DWORD i = 0; all && i < info->chStores; i++)
        CertAddStoreToCollection(all, info->pahStores[i], CERT_PHYSICAL_STORE_ADD_ENABLE_FLAG, 0);
    return all;
}

LONG WtVerifyHttps(const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    const WINTRUST_CERT_INFO *info = options->pCert;
    HTTPSPolicyCallbackData *policy = options->pPolicyCallbackData;
    if (!info || info->cbStruct < offsetof(WINTRUST_CERT_INFO, dwFlags) || !info->psCertContext || !policy ||
        policy->cbSize < sizeof(*policy) || (options->dwProvFlags & WTD_USE_IE4_TRUST_FLAG)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return ERROR_INVALID_PARAMETER;
    }
    FILETIME when;
    GetSystemTimeAsFileTime(&when);
    if (info->cbStruct > offsetof(WINTRUST_CERT_INFO, psftVerifyAsOf) && info->psftVerifyAsOf)
        when = *info->psftVerifyAsOf;
    /* a server may show any of the three usages, a client the one. */
    static LPSTR client[] = {(LPSTR)szOID_PKIX_KP_CLIENT_AUTH};
    static LPSTR server[] = {(LPSTR)szOID_PKIX_KP_SERVER_AUTH, (LPSTR) "1.3.6.1.4.1.311.10.3.3",
                             (LPSTR) "2.16.840.1.113730.4.1"};
    CERT_CHAIN_PARA para = {.cbSize = offsetof(CERT_CHAIN_PARA, RequestedUsage) + sizeof(CERT_USAGE_MATCH)};
    para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    if (policy->dwAuthType == AUTHTYPE_CLIENT) {
        para.RequestedUsage.Usage.cUsageIdentifier = RTL_NUMBER_OF(client);
        para.RequestedUsage.Usage.rgpszUsageIdentifier = client;
    } else {
        para.RequestedUsage.Usage.cUsageIdentifier = RTL_NUMBER_OF(server);
        para.RequestedUsage.Usage.rgpszUsageIdentifier = server;
    }
    /* revocation is looked up for all but the root when the caller asks and does not ignore it. */
    DWORD flags = 0;
    if (!(policy->fdwChecks & SECURITY_FLAG_IGNORE_REVOCATION) && options->fdwRevocationChecks)
        flags |= CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT;
    if (policy->fdwChecks & SECURITY_FLAG_OPT_IN_WEAK_SIGNATURE)
        flags |= CERT_CHAIN_OPT_IN_WEAK_SIGNATURE;
    HCERTCHAINENGINE engine = restricted_engine(info);
    HCERTSTORE stores = additional_store(info);
    PCCERT_CHAIN_CONTEXT built = nullptr;
    BOOL ok = CertGetCertificateChain(engine, info->psCertContext, &when, stores, &para, flags, nullptr, &built);
    if (engine)
        CertFreeCertificateChainEngine(engine);
    if (stores)
        CertCloseStore(stores, 0);
    if (!ok)
        return TRUST_E_SYSTEM_ERROR;
    *chain = built;
    /*
     * a certificate whose signature does not hold ends the verification before the policy is asked, unless the
     * caller opted in to weak signatures and the chain is weak. a revoked one is left to the policy.
     */
    LONG broken = 0;
    for (DWORD i = 0; i < built->cChain; i++)
        for (DWORD j = 0; j < built->rgpChain[i]->cElement; j++) {
            DWORD error = built->rgpChain[i]->rgpElement[j]->TrustStatus.dwErrorStatus;
            if (error & CERT_TRUST_IS_NOT_SIGNATURE_VALID)
                broken = (error & CERT_TRUST_HAS_WEAK_SIGNATURE) && (options->dwProvFlags & WTD_DISABLE_MD2_MD4)
                             ? NTE_BAD_ALGID
                             : TRUST_E_CERT_SIGNATURE;
        }
    if (broken && !((policy->fdwChecks & SECURITY_FLAG_IGNORE_WEAK_SIGNATURE) &&
                    (built->TrustStatus.dwErrorStatus & CERT_TRUST_HAS_WEAK_SIGNATURE)))
        return broken;
    CERT_CHAIN_POLICY_PARA policy_para = {.cbSize = sizeof(policy_para), .pvExtraPolicyPara = policy};
    CERT_CHAIN_POLICY_STATUS status = {.cbSize = sizeof(status)};
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, built, &policy_para, &status))
        return TRUST_E_SYSTEM_ERROR;
    return (LONG)status.dwError;
}
