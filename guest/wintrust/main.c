#include "trust_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/* one line per verification call to the debug output, so files and results are in the host's log. */
void WtTrace(const char *format, ...)
{
    char text[600];
    int n = _snprintf(text, sizeof(text) - 2, "wt %u ", GetCurrentProcessId());
    va_list ap;
    va_start(ap, format);
    int m = _vsnprintf(text + n, sizeof(text) - 2 - n, format, ap);
    va_end(ap);
    if (m < 0)
        m = (int)strlen(text + n);
    n += m;
    text[n++] = 0;
    OutputDebugStringA(text);
}

static void narrow(char *out, DWORD size, const WCHAR *in)
{
    DWORD i = 0;
    for (; in && in[i] && i + 1 < size; i++)
        out[i] = in[i] < 0x80 ? (char)in[i] : '?';
    out[i] = 0;
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

/* the signature file of an app package: its subject is named by this identifier. */
static LONG verify_blob(const WINTRUST_DATA *data, PCCERT_CHAIN_CONTEXT *chain)
{
    static const GUID p7x = {0x5598cff1, 0x68db, 0x4340, {0xb5, 0x7f, 0x1c, 0xac, 0xf8, 0x8c, 0x9a, 0x51}};
    const WINTRUST_BLOB_INFO *info = data->pBlob;
    if (!info || info->cbStruct < offsetof(WINTRUST_BLOB_INFO, cbMemSignedMsg) || !info->pbMemObject)
        return E_INVALIDARG;
    if (memcmp(&info->gSubject, &p7x, sizeof(p7x)))
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    return WtVerifyP7x(info->pbMemObject, info->cbMemObject, data, chain);
}

/* the kind of the file comes from its first bytes. cabinets and packages are mapped whole, executables in views. */
static LONG verify_mapping(HANDLE mapping, DWORD size, const WINTRUST_DATA *data, PCCERT_CHAIN_CONTEXT *chain)
{
    if (!mapping)
        return WtVerifyPe(nullptr, 0, data, chain);
    BYTE magic[8] = {};
    DWORD got = size < sizeof(magic) ? size : sizeof(magic);
    const BYTE *start = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, got);
    if (!start)
        return HRESULT_FROM_WIN32(GetLastError());
    memcpy(magic, start, got);
    UnmapViewOfFile(start);
    BOOL package = got == 8 && !memcmp(magic, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8);
    BOOL cabinet = got >= 4 && !memcmp(magic, "MSCF", 4);
    /* a catalog is a bare pkcs#7 signed data, a DER sequence. */
    BOOL catalog = got >= 2 && magic[0] == 0x30 && (magic[1] == 0x82 || magic[1] == 0x81);
    if (!package && !cabinet && !catalog)
        return WtVerifyPe(mapping, size, data, chain);
    const BYTE *bytes = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!bytes)
        return HRESULT_FROM_WIN32(GetLastError());
    LONG result = package ? WtVerifyInstaller(bytes, size, data, chain)
                 : cabinet ? WtVerifyCab(bytes, size, data, chain)
                           : WtVerifyCatalog(bytes, size, data, chain);
    UnmapViewOfFile(bytes);
    return result;
}

static LONG verify_file(const WINTRUST_DATA *data, PCCERT_CHAIN_CONTEXT *chain)
{
    WINTRUST_FILE_INFO *info = data->pFile;
    /* pgKnownSubject came later, callers of the older size leave it out and only a larger size is read. */
    if (!info || info->cbStruct < offsetof(WINTRUST_FILE_INFO, pgKnownSubject) ||
        (info->cbStruct >= sizeof(*info) && info->pgKnownSubject) ||
        (!info->hFile && (!info->pcwszFilePath || !*info->pcwszFilePath)))
        return E_INVALIDARG;
    HANDLE file = info->hFile;
    BOOL owned = !file;
    if (owned)
        file = CreateFileW(info->pcwszFilePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return owned ? HRESULT_FROM_WIN32(GetLastError()) : E_INVALIDARG;
    /*
     * the file is mapped, not read, so its position stays and a large cabinet costs no copy. an executable is taken in views,
     * as the setup programs of Autodesk are a gigabyte in a process of 32 bits.
     */
    LARGE_INTEGER size;
    HANDLE mapping = nullptr;
    LONG result = 0;
    if (!GetFileSizeEx(file, &size))
        result = HRESULT_FROM_WIN32(GetLastError());
    else if (size.QuadPart < 0 || size.QuadPart > 0xFFFFFFFF)
        result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    else if (size.QuadPart && !(mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)))
        result = HRESULT_FROM_WIN32(GetLastError());
    /*
     * the files of the system image are the operating system: windows signs its own, so an unsigned one of mime's is
     * as trusted. the file system numbers them like ntfs records of sequence 1 from 0x1000, which no host file has.
     */
    BY_HANDLE_FILE_INFORMATION about;
    BOOL system_image = GetFileInformationByHandle(file, &about) && about.nFileIndexHigh == 0x10000 &&
                        about.nFileIndexLow >= 0x1000 && about.nFileIndexLow < 0x100000;
    if (owned)
        CloseHandle(file);
    if (!result)
        result = verify_mapping(mapping, (DWORD)size.QuadPart, data, chain);
    if (mapping)
        CloseHandle(mapping);
    if (result == TRUST_E_NOSIGNATURE && system_image)
        result = ERROR_SUCCESS;
    WtTrace("file system_image %u result %08lx", system_image, (ULONG)result);
    return result;
}

/* a file verified against a catalog: what is checked here is the catalog's own signature. */
static LONG verify_catalog(const WINTRUST_DATA *data, PCCERT_CHAIN_CONTEXT *chain)
{
    const WINTRUST_CATALOG_INFO *info = data->pCatalog;
    if (!info || info->cbStruct < offsetof(WINTRUST_CATALOG_INFO, pcwszCatalogFilePath) + sizeof(LPCWSTR) ||
        !info->pcwszCatalogFilePath)
        return E_INVALIDARG;
    HANDLE file = CreateFileW(info->pcwszCatalogFilePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    LONG result = 0;
    HANDLE mapping = nullptr;
    if (!GetFileSizeEx(file, &size))
        result = HRESULT_FROM_WIN32(GetLastError());
    else if (size.QuadPart <= 0 || size.QuadPart > 0xFFFFFFFF)
        result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    else if (!(mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)))
        result = HRESULT_FROM_WIN32(GetLastError());
    if (!result) {
        const BYTE *bytes = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (!bytes)
            result = HRESULT_FROM_WIN32(GetLastError());
        else {
            result = WtVerifyCatalog(bytes, (DWORD)size.QuadPart, data, chain);
            UnmapViewOfFile(bytes);
        }
    }
    if (mapping)
        CloseHandle(mapping);
    CloseHandle(file);
    return result;
}

LONG WINAPI WinVerifyTrust(HWND window, GUID *action, PVOID input)
{
    static const GUID generic = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    static const GUID driver = DRIVER_ACTION_VERIFY;
    WINTRUST_DATA *data = input;
    char path[420] = "";
    if (data && action && data->dwUnionChoice == WTD_CHOICE_FILE && data->pFile)
        narrow(path, sizeof(path), data->pFile->pcwszFilePath);
    else if (data && action && data->dwUnionChoice == WTD_CHOICE_CATALOG && data->pCatalog)
        narrow(path, sizeof(path), ((const WINTRUST_CATALOG_INFO *)data->pCatalog)->pcwszCatalogFilePath);
    WtTrace("verify choice %u state %u file %s", data ? data->dwUnionChoice : 0, data ? data->dwStateAction : 0, path);
    if (!action || !data || (data->cbStruct != offsetof(WINTRUST_DATA, pSignatureSettings) &&
        data->cbStruct != sizeof(*data)))
        return E_INVALIDARG;
    if (memcmp(action, &generic, sizeof(generic)) && memcmp(action, &driver, sizeof(driver)))
        return TRUST_E_ACTION_UNKNOWN;
    if (data->dwStateAction == WTD_STATEACTION_CLOSE)
        return state_close(data);
    if (data->dwStateAction > WTD_STATEACTION_VERIFY || data->pPolicyCallbackData || data->pSIPClientData ||
        data->dwUIChoice < WTD_UI_ALL || data->dwUIChoice > WTD_UI_NOGOOD || data->fdwRevocationChecks > 1 ||
        (data->cbStruct == sizeof(*data) && data->pSignatureSettings))
        return E_INVALIDARG;
    if (data->dwUnionChoice != WTD_CHOICE_FILE && data->dwUnionChoice != WTD_CHOICE_BLOB &&
        data->dwUnionChoice != WTD_CHOICE_CATALOG)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    LONG (*verify)(const WINTRUST_DATA *, PCCERT_CHAIN_CONTEXT *) =
        data->dwUnionChoice == WTD_CHOICE_BLOB   ? verify_blob
        : data->dwUnionChoice == WTD_CHOICE_CATALOG ? verify_catalog
                                                    : verify_file;
    if (data->dwStateAction != WTD_STATEACTION_VERIFY)
        return verify(data, nullptr);
    if (data->hWVTStateData)
        return E_INVALIDARG;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    LONG result = verify(data, &chain);
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
