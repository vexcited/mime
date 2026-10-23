#include "trust_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/*
 * the verification handle is CRYPT_PROVIDER_DATA, containing one signer and its certificate chain from signer to
 * root.
 */
#define STATE_MAGIC 0x54535457 /* "WTST". */

typedef struct WT_STATE {
    CRYPT_PROVIDER_DATA Data;
    CRYPT_PROVIDER_SGNR Signer;
    DWORD Magic;
    PCCERT_CHAIN_CONTEXT Chain;
    CRYPT_PROVIDER_CERT Certs[];
} WT_STATE;

static WT_STATE *state_open(WINTRUST_DATA *data, PCCERT_CHAIN_CONTEXT chain, LONG result)
{
    DWORD count = chain && chain->cChain ? chain->rgpChain[0]->cElement : 0;
    WT_STATE *state = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*state) + count * sizeof(CRYPT_PROVIDER_CERT));
    if (!state)
        return nullptr;
    static GUID generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    state->Magic = STATE_MAGIC;
    state->Chain = chain;
    state->Data.cbStruct = sizeof(state->Data);
    state->Data.pWintrustData = data;
    state->Data.pgActionID = &generic;
    state->Data.dwEncoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    state->Data.dwSubjectChoice = data->dwUnionChoice;
    state->Data.dwProvFlags = data->dwProvFlags;
    state->Data.dwFinalError = (DWORD)result;
    GetSystemTimeAsFileTime(&state->Data.sftSystemTime);
    if (chain) {
        state->Data.csSigners = 1;
        state->Data.pasSigners = &state->Signer;
        state->Signer.cbStruct = sizeof(state->Signer);
        state->Signer.sftVerifyAsOf = state->Data.sftSystemTime;
        state->Signer.csCertChain = count;
        state->Signer.pasCertChain = state->Certs;
        state->Signer.pChainContext = chain;
        state->Signer.dwError = (DWORD)result;
        for (DWORD i = 0; i < count; i++) {
            PCERT_CHAIN_ELEMENT element = chain->rgpChain[0]->rgpElement[i];
            CRYPT_PROVIDER_CERT *cert = &state->Certs[i];
            cert->cbStruct = sizeof(*cert);
            cert->pCert = element->pCertContext;
            cert->pChainElement = element;
            cert->fSelfSigned = (element->TrustStatus.dwInfoStatus & CERT_TRUST_IS_SELF_SIGNED) != 0;
            cert->fTrustedRoot = cert->fSelfSigned && !(element->TrustStatus.dwErrorStatus & CERT_TRUST_IS_UNTRUSTED_ROOT);
            cert->fCommercial = TRUE;
        }
    }
    data->hWVTStateData = state;
    return state;
}

static LONG state_close(WINTRUST_DATA *data)
{
    WT_STATE *state = data->hWVTStateData;
    if (!state)
        return 0;
    if (state->Magic != STATE_MAGIC)
        return E_INVALIDARG;
    state->Magic = 0;
    if (state->Chain)
        CertFreeCertificateChain(state->Chain);
    LocalFree(state);
    data->hWVTStateData = nullptr;
    return 0;
}

WINTRUSTAPI CRYPT_PROVIDER_DATA *WINAPI WTHelperProvDataFromStateData(HANDLE state)
{
    return state;
}

WINTRUSTAPI CRYPT_PROVIDER_SGNR *WINAPI WTHelperGetProvSignerFromChain(CRYPT_PROVIDER_DATA *data, DWORD signer,
                                                                    BOOL counter, DWORD counter_index)
{
    if (!data || signer >= data->csSigners)
        return nullptr;
    CRYPT_PROVIDER_SGNR *s = &data->pasSigners[signer];
    if (!counter)
        return s;
    return counter_index < s->csCounterSigners ? &s->pasCounterSigners[counter_index] : nullptr;
}

WINTRUSTAPI CRYPT_PROVIDER_CERT *WINAPI WTHelperGetProvCertFromChain(CRYPT_PROVIDER_SGNR *signer, DWORD index)
{
    return signer && index < signer->csCertChain ? &signer->pasCertChain[index] : nullptr;
}

static LONG verify_file(const WINTRUST_DATA *data, PCCERT_CHAIN_CONTEXT *chain)
{
    WINTRUST_FILE_INFO *info = data->pFile;
    if (!info || info->cbStruct != sizeof(*info) || info->pgKnownSubject ||
        (!info->hFile && (!info->pcwszFilePath || !*info->pcwszFilePath)))
        return E_INVALIDARG;
    HANDLE file = info->hFile;
    BOOL owned = !file;
    if (owned)
        file = CreateFileW(info->pcwszFilePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return owned ? HRESULT_FROM_WIN32(GetLastError()) : E_INVALIDARG;
    LARGE_INTEGER size, position, zero = {};
    BYTE *bytes = nullptr;
    LONG result = 0;
    BOOL restore = FALSE;
    if (!GetFileSizeEx(file, &size))
        result = HRESULT_FROM_WIN32(GetLastError());
    else if (size.QuadPart < 0 || size.QuadPart > 256 * 1024 * 1024)
        result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    else if (!(bytes = LocalAlloc(LMEM_FIXED, size.QuadPart ? (SIZE_T)size.QuadPart : 1)))
        result = E_OUTOFMEMORY;
    else if (!SetFilePointerEx(file, zero, &position, FILE_CURRENT))
        result = HRESULT_FROM_WIN32(GetLastError());
    else {
        restore = !owned;
        DWORD read;
        if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) || !ReadFile(file, bytes, (DWORD)size.QuadPart, &read, nullptr))
            result = HRESULT_FROM_WIN32(GetLastError());
        else if (read != size.QuadPart)
            result = HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    }
    if (restore && !SetFilePointerEx(file, position, nullptr, FILE_BEGIN) && !result)
        result = HRESULT_FROM_WIN32(GetLastError());
    if (owned)
        CloseHandle(file);
    if (!result)
        result = size.QuadPart >= 8 && !memcmp(bytes, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8) ?
                     WtVerifyInstaller(bytes, (DWORD)size.QuadPart, data, chain) :
                 size.QuadPart >= 4 && !memcmp(bytes, "MSCF", 4) ? WtVerifyCab(bytes, (DWORD)size.QuadPart, data, chain)
                                                                  : WtVerifyPe(bytes, (DWORD)size.QuadPart, data, chain);
    LocalFree(bytes);
    return result;
}

LONG WINAPI WinVerifyTrust(HWND window, GUID *action, PVOID input)
{
    static const GUID generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA *data = input;
    if (!action || !data || (data->cbStruct != offsetof(WINTRUST_DATA, pSignatureSettings) &&
        data->cbStruct != sizeof(*data)))
        return E_INVALIDARG;
    if (memcmp(action, &generic, sizeof(generic)))
        return TRUST_E_ACTION_UNKNOWN;
    if (data->dwStateAction == WTD_STATEACTION_CLOSE)
        return state_close(data);
    if (data->dwStateAction > WTD_STATEACTION_VERIFY || data->pPolicyCallbackData || data->pSIPClientData ||
        data->dwUIChoice < WTD_UI_ALL || data->dwUIChoice > WTD_UI_NOGOOD || data->fdwRevocationChecks > 1 ||
        (data->cbStruct == sizeof(*data) && data->pSignatureSettings))
        return E_INVALIDARG;
    if (data->dwUnionChoice != WTD_CHOICE_FILE)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (data->dwStateAction != WTD_STATEACTION_VERIFY)
        return verify_file(data, nullptr);
    if (data->hWVTStateData)
        return E_INVALIDARG;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    LONG result = verify_file(data, &chain);
    if (!state_open(data, chain, result)) {
        if (chain)
            CertFreeCertificateChain(chain);
        return E_OUTOFMEMORY;
    }
    return result;
}

HRESULT WINAPI WinVerifyTrustEx(HWND window, GUID *action, WINTRUST_DATA *data)
{
    return WinVerifyTrust(window, action, data);
}
