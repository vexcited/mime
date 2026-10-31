/* the viewers of certificates, revocation lists, trust lists and signers, and the dialogs that choose a certificate or a store. */
#include "cryptui_private.h"

#define LINE L"\r\n"

typedef struct {
    DWORD dwSize;
    HWND hwndParent;
    DWORD dwFlags;
    const void *szTitle;
    const void *pContext;
} VIEW_STRUCT;

typedef struct {
    DWORD dwSize;
    HWND hwndParent;
    DWORD dwFlags;
    const void *szTitle;
    void *pSignerInfo;
    HANDLE hMsg;
    LPCSTR pszOID;
} SIGNER_STRUCT;

typedef BOOL(WINAPI *CERT_FILTER)(PCCERT_CONTEXT, BOOL *, void *);

typedef struct {
    DWORD dwSize;
    HWND hwndParent;
    DWORD dwFlags;
    const void *szTitle;
    DWORD dwDontUseColumn;
    const void *szDisplayString;
    CERT_FILTER pFilterCallback;
    void *pDisplayCallback;
    void *pvCallbackData;
    DWORD cDisplayStores;
    HCERTSTORE *rghDisplayStores;
    DWORD cStores;
    HCERTSTORE *rghStores;
    DWORD cPropSheetPages;
    const void *rgPropSheetPages;
    HCERTSTORE hSelectedCertStore;
} SELECT_CERT_STRUCT;

typedef struct {
    DWORD dwSize;
    HWND hwndParent;
    DWORD dwFlags;
    const void *pwszTitle;
    const void *pwszText;
    void *pEnumData;
    void *pfnSelectedStoreCallback;
    void *pvArg;
} SELECT_STORE_STRUCT;

/* ------------------------------------------------------------------ the text of the details */

static void add_line(WCHAR *text, SIZE_T size, const WCHAR *label, const WCHAR *value)
{
    CuiAppend(text, size, label);
    CuiAppend(text, size, value ? value : L"");
    CuiAppend(text, size, LINE);
}

static void hex(WCHAR *out, const BYTE *bytes, DWORD count, BOOL reversed)
{
    static const WCHAR digits[] = L"0123456789abcdef";
    for (DWORD i = 0; i < count; i++) {
        BYTE value = bytes[reversed ? count - 1 - i : i];
        out[i * 3] = digits[value >> 4];
        out[i * 3 + 1] = digits[value & 15];
        out[i * 3 + 2] = ' ';
    }
    out[count ? count * 3 - 1 : 0] = 0;
}

static void add_date_line(WCHAR *text, SIZE_T size, const WCHAR *label, FILETIME time)
{
    WCHAR *date = nullptr;
    CuiDateString(&date, time, TRUE);
    add_line(text, size, label, date);
    LocalFree(date);
}

static void add_blob_line(WCHAR *text, SIZE_T size, const WCHAR *label, const CRYPT_DATA_BLOB *blob, BOOL reversed)
{
    WCHAR line[256];
    DWORD count = blob->cbData > 80 ? 80 : blob->cbData;
    hex(line, blob->pbData, count, reversed);
    add_line(text, size, label, line);
}

WCHAR *CuiCertDetails(PCCERT_CONTEXT cert)
{
    SIZE_T size = 8192;
    WCHAR *text = LocalAlloc(0x40, size * sizeof(WCHAR));
    if (!text)
        return nullptr;
    CuiAppend(text, size, L"Certificate Information" LINE LINE);
    WCHAR *usage = nullptr;
    CuiUsageString(&usage, cert, FALSE);
    CuiAppend(text, size, L"This certificate is intended for the following purpose(s):" LINE);
    add_line(text, size, L"    ", usage);
    LocalFree(usage);
    CuiAppend(text, size, LINE);
    WCHAR *name = CuiCertName(cert, FALSE);
    add_line(text, size, L"Issued to: ", name);
    LocalFree(name);
    name = CuiCertName(cert, TRUE);
    add_line(text, size, L"Issued by: ", name);
    LocalFree(name);
    if (cert->pCertInfo) {
        add_date_line(text, size, L"Valid from: ", cert->pCertInfo->NotBefore);
        add_date_line(text, size, L"Valid to: ", cert->pCertInfo->NotAfter);
        add_blob_line(text, size, L"Serial number: ", &cert->pCertInfo->SerialNumber, TRUE);
        if (cert->pCertInfo->SignatureAlgorithm.pszObjId) {
            WCHAR *oid = CuiAnsiToWide(cert->pCertInfo->SignatureAlgorithm.pszObjId);
            add_line(text, size, L"Signature algorithm: ", oid);
            LocalFree(oid);
        }
    }
    BYTE hash[20];
    DWORD hash_size = sizeof(hash);
    if (CUI_PROC(CertGetCertificateContextProperty)(cert, 3, hash, &hash_size)) {
        WCHAR line[80];
        hex(line, hash, hash_size, FALSE);
        add_line(text, size, L"Thumbprint: ", line);
    }
    DWORD key_size = 0;
    if (CUI_PROC(CertGetCertificateContextProperty)(cert, 2, nullptr, &key_size))
        CuiAppend(text, size, LINE L"You have a private key that corresponds to this certificate." LINE);
    return text;
}

static WCHAR *name_of_blob(const CRYPT_DATA_BLOB *name)
{
    DWORD count = CUI_PROC(CertNameToStrW)(X509_ASN_ENCODING, (PCERT_NAME_BLOB)name, CERT_SIMPLE_NAME_STR, nullptr, 0);
    WCHAR *text = count ? LocalAlloc(0x40, count * sizeof(WCHAR)) : nullptr;
    if (text)
        CUI_PROC(CertNameToStrW)(X509_ASN_ENCODING, (PCERT_NAME_BLOB)name, CERT_SIMPLE_NAME_STR, text, count);
    return text;
}

static WCHAR *crl_details(const CUI_CRL_CONTEXT *crl)
{
    SIZE_T size = 16384;
    WCHAR *text = LocalAlloc(0x40, size * sizeof(WCHAR));
    if (!text)
        return nullptr;
    const CUI_CRL_INFO *info = crl->pCrlInfo;
    CuiAppend(text, size, L"Certificate Revocation List Information" LINE LINE);
    WCHAR *issuer = name_of_blob(&info->Issuer);
    add_line(text, size, L"Issuer: ", issuer);
    LocalFree(issuer);
    add_date_line(text, size, L"Effective date: ", info->ThisUpdate);
    if (info->NextUpdate.dwLowDateTime || info->NextUpdate.dwHighDateTime)
        add_date_line(text, size, L"Next update: ", info->NextUpdate);
    CuiAppend(text, size, LINE L"Revoked certificates:" LINE);
    for (DWORD i = 0; i < info->cCRLEntry && i < 200; i++) {
        add_blob_line(text, size, L"    Serial number: ", &info->rgCRLEntry[i].SerialNumber, TRUE);
        add_date_line(text, size, L"    Revocation date: ", info->rgCRLEntry[i].RevocationDate);
    }
    return text;
}

static WCHAR *ctl_details(const CUI_CTL_CONTEXT *ctl)
{
    SIZE_T size = 16384;
    WCHAR *text = LocalAlloc(0x40, size * sizeof(WCHAR));
    if (!text)
        return nullptr;
    const CUI_CTL_INFO *info = ctl->pCtlInfo;
    CuiAppend(text, size, L"Certificate Trust List Information" LINE LINE);
    add_blob_line(text, size, L"List identifier: ", &info->ListIdentifier, FALSE);
    add_blob_line(text, size, L"Sequence number: ", &info->SequenceNumber, TRUE);
    add_date_line(text, size, L"Effective date: ", info->ThisUpdate);
    if (info->NextUpdate.dwLowDateTime || info->NextUpdate.dwHighDateTime)
        add_date_line(text, size, L"Next update: ", info->NextUpdate);
    CuiAppend(text, size, LINE L"Subject usage:" LINE);
    for (DWORD i = 0; i < info->cUsageIdentifier; i++) {
        WCHAR *oid = CuiAnsiToWide(info->rgpszUsageIdentifier[i]);
        add_line(text, size, L"    ", oid);
        LocalFree(oid);
    }
    CuiAppend(text, size, LINE L"Trusted subjects:" LINE);
    for (DWORD i = 0; i < info->cCTLEntry && i < 200; i++)
        add_blob_line(text, size, L"    Hash value: ", &info->rgCTLEntry[i].SubjectIdentifier, FALSE);
    return text;
}

static void show_owned(HWND parent, const WCHAR *title, const WCHAR *fallback, WCHAR *text)
{
    if (!text)
        return;
    CuiShowText(parent, title ? title : fallback, text);
    LocalFree(text);
}

/* ------------------------------------------------------------------ the viewers */

BOOL WINAPI CryptUIDlgViewCertificateW(PCCRYPTUI_VIEWCERTIFICATE_STRUCTW info, BOOL *changed)
{
    if (!info || info->dwSize != sizeof(*info)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    if (changed)
        *changed = FALSE;
    if (!info->pCertContext) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    show_owned(info->hwndParent, info->szTitle, L"Certificate", CuiCertDetails(info->pCertContext));
    return TRUE;
}

BOOL WINAPI CryptUIDlgViewCertificateA(const CRYPTUI_VIEWCERTIFICATE_STRUCTW *info, BOOL *changed)
{
    if (!info || info->dwSize != sizeof(*info)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    CRYPTUI_VIEWCERTIFICATE_STRUCTW wide = *info;
    WCHAR *title = CuiAnsiToWide((const char *)info->szTitle);
    wide.szTitle = title;
    BOOL result = CryptUIDlgViewCertificateW(&wide, changed);
    LocalFree(title);
    return result;
}

BOOL WINAPI CryptUIDlgViewCertificatePropertiesW(const VIEW_STRUCT *info, BOOL *changed)
{
    if (!info || info->dwSize != CUI_SIZE(88, 44)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    if (changed)
        *changed = FALSE;
    if (!info->pContext) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    show_owned(info->hwndParent, info->szTitle, L"Certificate Properties", CuiCertDetails(info->pContext));
    return TRUE;
}

BOOL WINAPI CryptUIDlgViewCertificatePropertiesA(const VIEW_STRUCT *info, BOOL *changed)
{
    if (!info || info->dwSize != CUI_SIZE(88, 44)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    VIEW_STRUCT wide = *info;
    WCHAR *title = CuiAnsiToWide(info->szTitle);
    wide.szTitle = title;
    BOOL result = CryptUIDlgViewCertificatePropertiesW(&wide, changed);
    LocalFree(title);
    return result;
}

BOOL WINAPI CryptUIDlgViewCRLW(const VIEW_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(72, 36)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    if (!info->pContext) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    show_owned(info->hwndParent, info->szTitle, L"Certificate Revocation List", crl_details(info->pContext));
    return TRUE;
}

BOOL WINAPI CryptUIDlgViewCRLA(const VIEW_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(72, 36)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    VIEW_STRUCT wide = *info;
    WCHAR *title = CuiAnsiToWide(info->szTitle);
    wide.szTitle = title;
    BOOL result = CryptUIDlgViewCRLW(&wide);
    LocalFree(title);
    return result;
}

BOOL WINAPI CryptUIDlgViewCTLW(const VIEW_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(88, 44)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    if (!info->pContext) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    show_owned(info->hwndParent, info->szTitle, L"Certificate Trust List", ctl_details(info->pContext));
    return TRUE;
}

BOOL WINAPI CryptUIDlgViewCTLA(const VIEW_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(88, 44)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    VIEW_STRUCT wide = *info;
    WCHAR *title = CuiAnsiToWide(info->szTitle);
    wide.szTitle = title;
    BOOL result = CryptUIDlgViewCTLW(&wide);
    LocalFree(title);
    return result;
}

BOOL WINAPI CryptUIDlgViewContext(DWORD type, const void *context, HWND parent, LPCWSTR title, DWORD flags,
                                  void *reserved)
{
    if (type == CERT_STORE_CERTIFICATE_CONTEXT) {
        CRYPTUI_VIEWCERTIFICATE_STRUCTW info = {};
        info.dwSize = sizeof(info);
        info.hwndParent = parent;
        info.szTitle = title;
        info.pCertContext = context;
        return CryptUIDlgViewCertificateW(&info, nullptr);
    }
    if (type == CERT_STORE_CRL_CONTEXT || type == CERT_STORE_CTL_CONTEXT) {
        VIEW_STRUCT info = {type == CERT_STORE_CRL_CONTEXT ? CUI_SIZE(72, 36) : CUI_SIZE(88, 44), parent, 0, title,
                            context};
        return type == CERT_STORE_CRL_CONTEXT ? CryptUIDlgViewCRLW(&info) : CryptUIDlgViewCTLW(&info);
    }
    SetLastError(CUI_E_INVALIDARG);
    return FALSE;
}

/* the signer of a signed message: who made it and with which certificate, named by issuer and serial number. */
BOOL WINAPI CryptUIDlgViewSignerInfoW(const SIGNER_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(56, 28) || !info->pSignerInfo) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    const CMSG_SIGNER_INFO *signer = info->pSignerInfo;
    SIZE_T size = 4096;
    WCHAR *text = LocalAlloc(0x40, size * sizeof(WCHAR));
    if (!text)
        return FALSE;
    CuiAppend(text, size, L"Digital Signature Information" LINE LINE);
    WCHAR *issuer = name_of_blob((const CRYPT_DATA_BLOB *)&signer->Issuer);
    add_line(text, size, L"Certificate issuer: ", issuer);
    LocalFree(issuer);
    add_blob_line(text, size, L"Certificate serial number: ", (const CRYPT_DATA_BLOB *)&signer->SerialNumber, TRUE);
    if (signer->HashAlgorithm.pszObjId) {
        WCHAR *oid = CuiAnsiToWide(signer->HashAlgorithm.pszObjId);
        add_line(text, size, L"Digest algorithm: ", oid);
        LocalFree(oid);
    }
    show_owned(info->hwndParent, info->szTitle, L"Digital Signature Details", text);
    return TRUE;
}

BOOL WINAPI CryptUIDlgViewSignerInfoA(const SIGNER_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(56, 28)) {
        SetLastError(CUI_E_INVALIDARG);
        return FALSE;
    }
    SIGNER_STRUCT wide = *info;
    WCHAR *title = CuiAnsiToWide(info->szTitle);
    wide.szTitle = title;
    BOOL result = CryptUIDlgViewSignerInfoW(&wide);
    LocalFree(title);
    return result;
}

/* ------------------------------------------------------------------ the choices */

typedef struct {
    PCCERT_CONTEXT *certificates;
    WCHAR **names;
    UINT count, capacity;
} CHOICES;

static void add_choice(CHOICES *choices, PCCERT_CONTEXT cert)
{
    if (choices->count == choices->capacity) {
        UINT capacity = choices->capacity ? choices->capacity * 2 : 16;
        PCCERT_CONTEXT *certificates = LocalAlloc(0x40, capacity * sizeof(*certificates));
        WCHAR **names = LocalAlloc(0x40, capacity * sizeof(*names));
        if (!certificates || !names) {
            LocalFree(certificates);
            LocalFree(names);
            return;
        }
        for (UINT i = 0; i < choices->count; i++) {
            certificates[i] = choices->certificates[i];
            names[i] = choices->names[i];
        }
        LocalFree(choices->certificates);
        LocalFree(choices->names);
        choices->certificates = certificates;
        choices->names = names;
        choices->capacity = capacity;
    }
    WCHAR line[640] = {};
    WCHAR *subject = CuiCertName(cert, FALSE), *issuer = CuiCertName(cert, TRUE);
    CuiAppend(line, 640, subject);
    CuiAppend(line, 640, L"    (issued by ");
    CuiAppend(line, 640, issuer);
    CuiAppend(line, 640, L")");
    LocalFree(subject);
    LocalFree(issuer);
    choices->certificates[choices->count] = CUI_PROC(CertDuplicateCertificateContext)(cert);
    choices->names[choices->count++] = CuiCopy(line);
}

static void free_choices(CHOICES *choices)
{
    for (UINT i = 0; i < choices->count; i++) {
        CUI_PROC(CertFreeCertificateContext)(choices->certificates[i]);
        LocalFree(choices->names[i]);
    }
    LocalFree(choices->certificates);
    LocalFree(choices->names);
}

PCCERT_CONTEXT WINAPI CryptUIDlgSelectCertificateW(SELECT_CERT_STRUCT *info)
{
    if (!info || (info->dwSize != CUI_SIZE(128, 64) && info->dwSize != CUI_SIZE(120, 60))) {
        SetLastError(CUI_E_INVALIDARG);
        return nullptr;
    }
    CHOICES choices = {};
    HCERTSTORE own = nullptr;
    HCERTSTORE *stores = info->rghDisplayStores;
    DWORD count = info->cDisplayStores;
    /* without a store to show, the personal certificates of the user. */
    if (!count) {
        own = CUI_PROC(CertOpenStore)(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG,
                                      L"MY");
        stores = &own;
        count = own != nullptr;
    }
    for (DWORD i = 0; i < count; i++)
        for (PCCERT_CONTEXT cert = CUI_PROC(CertEnumCertificatesInStore)(stores[i], nullptr); cert;
             cert = CUI_PROC(CertEnumCertificatesInStore)(stores[i], cert)) {
            BOOL selected = FALSE;
            if (!info->pFilterCallback || info->pFilterCallback(cert, &selected, info->pvCallbackData))
                add_choice(&choices, cert);
        }
    PCCERT_CONTEXT result = nullptr;
    int chosen = CuiPickFromList(info->hwndParent, info->szTitle ? info->szTitle : L"Select a Certificate",
                                 info->szDisplayString ? info->szDisplayString : L"Select a certificate.", choices.names,
                                 choices.count);
    if (chosen >= 0)
        result = CUI_PROC(CertDuplicateCertificateContext)(choices.certificates[chosen]);
    free_choices(&choices);
    if (own)
        CUI_PROC(CertCloseStore)(own, 0);
    SetLastError(ERROR_SUCCESS);
    return result;
}

PCCERT_CONTEXT WINAPI CryptUIDlgSelectCertificateA(const SELECT_CERT_STRUCT *info)
{
    if (!info || (info->dwSize != CUI_SIZE(128, 64) && info->dwSize != CUI_SIZE(120, 60))) {
        SetLastError(CUI_E_INVALIDARG);
        return nullptr;
    }
    SELECT_CERT_STRUCT wide = *info;
    WCHAR *title = CuiAnsiToWide(info->szTitle), *text = CuiAnsiToWide(info->szDisplayString);
    wide.szTitle = title;
    wide.szDisplayString = text;
    PCCERT_CONTEXT result = CryptUIDlgSelectCertificateW(&wide);
    LocalFree(title);
    LocalFree(text);
    return result;
}

PCCERT_CONTEXT WINAPI CryptUIDlgSelectCertificateFromStore(HCERTSTORE store, HWND parent, LPCWSTR title, LPCWSTR text,
                                                           DWORD dont_use_column, DWORD flags, void *reserved)
{
    if (!store) {
        SetLastError(CUI_E_INVALIDARG);
        return nullptr;
    }
    SELECT_CERT_STRUCT info = {};
    info.dwSize = sizeof(info);
    info.hwndParent = parent;
    info.szTitle = title;
    info.dwDontUseColumn = dont_use_column;
    info.szDisplayString = text;
    info.cDisplayStores = 1;
    info.rghDisplayStores = &store;
    return CryptUIDlgSelectCertificateW(&info);
}

static BOOL WINAPI add_store_name(const void *name, DWORD flags, PCERT_SYSTEM_STORE_INFO info, void *reserved, void *argument)
{
    CHOICES *choices = argument;
    if (choices->count == choices->capacity) {
        UINT capacity = choices->capacity ? choices->capacity * 2 : 16;
        WCHAR **names = LocalAlloc(0x40, capacity * sizeof(*names));
        if (!names)
            return FALSE;
        for (UINT i = 0; i < choices->count; i++)
            names[i] = choices->names[i];
        LocalFree(choices->names);
        choices->names = names;
        choices->capacity = capacity;
    }
    choices->names[choices->count++] = CuiCopy(name);
    return TRUE;
}

HCERTSTORE WINAPI CryptUIDlgSelectStoreW(const SELECT_STORE_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(64, 32)) {
        SetLastError(CUI_E_INVALIDARG);
        return nullptr;
    }
    CHOICES choices = {};
    CUI_PROC(CertEnumSystemStore)(CERT_SYSTEM_STORE_CURRENT_USER, nullptr, &choices, add_store_name);
    HCERTSTORE store = nullptr;
    int chosen = CuiPickFromList(info->hwndParent, info->pwszTitle ? info->pwszTitle : L"Select Certificate Store",
                                 info->pwszText ? info->pwszText : L"Select a certificate store.", choices.names,
                                 choices.count);
    if (chosen >= 0)
        store = CUI_PROC(CertOpenStore)(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, choices.names[chosen]);
    for (UINT i = 0; i < choices.count; i++)
        LocalFree(choices.names[i]);
    LocalFree(choices.names);
    return store;
}

HCERTSTORE WINAPI CryptUIDlgSelectStoreA(const SELECT_STORE_STRUCT *info)
{
    if (!info || info->dwSize != CUI_SIZE(64, 32)) {
        SetLastError(CUI_E_INVALIDARG);
        return nullptr;
    }
    SELECT_STORE_STRUCT wide = *info;
    WCHAR *title = CuiAnsiToWide(info->pwszTitle), *text = CuiAnsiToWide(info->pwszText);
    wide.pwszTitle = title;
    wide.pwszText = text;
    HCERTSTORE store = CryptUIDlgSelectStoreW(&wide);
    LocalFree(title);
    LocalFree(text);
    return store;
}
