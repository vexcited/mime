/*
 * the wizards that import and export certificates without a window, the pages and contexts that need a property sheet or
 * the enrollment services, and the other calls of cryptui that have nothing behind them here.
 */
#include "cryptui_private.h"

#define WIZ_NO_UI 1
#define IMPORT_FILE 1
#define IMPORT_CERT_CONTEXT 2
#define IMPORT_STORE 5
#define EXPORT_DER 1
#define EXPORT_PFX 2
#define EXPORT_PKCS7 3
#define EXPORT_BASE64 4
#define EXPORT_CERT_CONTEXT 1
#define EXPORT_CERT_STORE 4

typedef struct {
    DWORD dwSize;
    DWORD dwSubjectChoice;
    union {
        LPCWSTR pwszFileName;
        PCCERT_CONTEXT pCertContext;
        HCERTSTORE hCertStore;
        const void *pContext;
    };
    DWORD dwFlags;
    LPCWSTR pwszPassword;
} IMPORT_SRC;

typedef struct {
    DWORD dwSize;
    LPCWSTR pwszExportFileName;
    DWORD dwSubjectChoice;
    union {
        PCCERT_CONTEXT pCertContext;
        HCERTSTORE hCertStore;
        const void *pContext;
    };
    DWORD cStores;
    HCERTSTORE *rghStores;
} EXPORT_INFO;

typedef struct {
    DWORD dwSize;
    DWORD dwExportFormat;
    BOOL fExportChain;
    BOOL fExportPrivateKeys;
    LPCWSTR pwszPassword;
    BOOL fStrongEncryption;
} EXPORT_CERT_INFO;

static BOOL fail(HRESULT error)
{
    SetLastError(error);
    return FALSE;
}

/* what a wizard that has no window answers: that it cannot be shown. */
static BOOL no_wizard(void)
{
    return fail(ERROR_CALL_NOT_IMPLEMENTED);
}

/* ------------------------------------------------------------------ import */

static BOOL read_file(LPCWSTR path, CRYPT_DATA_BLOB *blob)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    blob->pbData = size == INVALID_FILE_SIZE ? nullptr : LocalAlloc(0x40, size ? size : 1);
    blob->cbData = size;
    BOOL ok = blob->pbData && ReadFile(file, blob->pbData, size, &read, nullptr) && read == size;
    CloseHandle(file);
    if (!ok) {
        LocalFree(blob->pbData);
        blob->pbData = nullptr;
    }
    return ok;
}

/* the system store a certificate belongs in when none is named: a key goes to the personal ones, a root to the roots. */
static HCERTSTORE automatic_store(PCCERT_CONTEXT cert)
{
    DWORD size = 0;
    LPCWSTR name = L"CA";
    if (CUI_PROC(CertGetCertificateContextProperty)(cert, 2, nullptr, &size))
        name = L"MY";
    else if (CUI_PROC(CertCompareCertificateName)(X509_ASN_ENCODING, &cert->pCertInfo->Subject, &cert->pCertInfo->Issuer))
        name = L"ROOT";
    return CUI_PROC(CertOpenStore)(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, name);
}

static BOOL add_certificate(HCERTSTORE destination, PCCERT_CONTEXT cert)
{
    HCERTSTORE own = destination ? nullptr : automatic_store(cert);
    HCERTSTORE target = destination ? destination : own;
    BOOL ok = target && CUI_PROC(CertAddCertificateContextToStore)(target, cert, CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
    DWORD error = GetLastError();
    if (own)
        CUI_PROC(CertCloseStore)(own, 0);
    SetLastError(error);
    return ok;
}

static BOOL import_store(HCERTSTORE destination, HCERTSTORE source)
{
    BOOL ok = TRUE;
    for (PCCERT_CONTEXT cert = CUI_PROC(CertEnumCertificatesInStore)(source, nullptr); cert;
         cert = CUI_PROC(CertEnumCertificatesInStore)(source, cert))
        ok = add_certificate(destination, cert) && ok;
    return ok;
}

BOOL WINAPI CryptUIWizImport(DWORD flags, HWND parent, LPCWSTR title, const IMPORT_SRC *source, HCERTSTORE destination)
{
    if (!(flags & WIZ_NO_UI))
        return no_wizard();
    if (!source || source->dwSize != sizeof(*source) || !source->pContext)
        return fail(CUI_E_INVALIDARG);
    if (source->dwSubjectChoice == IMPORT_CERT_CONTEXT)
        return add_certificate(destination, source->pCertContext);
    if (source->dwSubjectChoice == IMPORT_STORE)
        return import_store(destination, source->hCertStore);
    if (source->dwSubjectChoice != IMPORT_FILE)
        return fail(CUI_E_INVALIDARG);

    CRYPT_DATA_BLOB blob;
    if (!read_file(source->pwszFileName, &blob))
        return FALSE;
    /* a text file holds the file in base 64, which is read back to the bytes it stands for. */
    DWORD binary_size = 0;
    if (blob.cbData && CUI_PROC(CryptStringToBinaryA)((LPCSTR)blob.pbData, blob.cbData, 7, nullptr, &binary_size, nullptr,
                                                       nullptr) && binary_size) {
        BYTE *binary = LocalAlloc(0x40, binary_size);
        if (binary && CUI_PROC(CryptStringToBinaryA)((LPCSTR)blob.pbData, blob.cbData, 7, binary, &binary_size, nullptr,
                                                     nullptr)) {
            LocalFree(blob.pbData);
            blob.pbData = binary;
            blob.cbData = binary_size;
        } else {
            LocalFree(binary);
        }
    }
    BOOL ok;
    DWORD error = ERROR_SUCCESS;
    PCCERT_CONTEXT single = CUI_PROC(CertCreateCertificateContext)(X509_ASN_ENCODING, blob.pbData, blob.cbData);
    if (single) {
        ok = add_certificate(destination, single);
        error = GetLastError();
        CUI_PROC(CertFreeCertificateContext)(single);
    } else {
        HCERTSTORE store = nullptr;
        DWORD content = 0, format = 0;
        if (!CUI_PROC(CryptQueryObject)(CERT_QUERY_OBJECT_BLOB, &blob,
                                        CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED | CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                        CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, &content, &format, &store, nullptr,
                                        nullptr) &&
            CUI_PROC(PFXIsPFXBlob)(&blob))
            store = CUI_PROC(PFXImportCertStore)(&blob, source->pwszPassword ? source->pwszPassword : L"", source->dwFlags);
        ok = store != nullptr;
        if (ok) {
            ok = import_store(destination, store);
            error = GetLastError();
            CUI_PROC(CertCloseStore)(store, 0);
        } else {
            error = 0x80092003;
        }
    }
    LocalFree(blob.pbData);
    SetLastError(error);
    return ok;
}

BOOL WINAPI CryptUIWizImportInternal(DWORD flags, HWND parent, LPCWSTR title, const IMPORT_SRC *source,
                                     HCERTSTORE destination)
{
    return CryptUIWizImport(flags, parent, title, source, destination);
}

/* ------------------------------------------------------------------ export */

static BOOL write_file(LPCWSTR path, const void *data, DWORD size)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return ok;
}

/* a length in the short or long form of DER. */
static BYTE *der_length(BYTE *out, DWORD length)
{
    if (length < 0x80) {
        *out++ = (BYTE)length;
    } else if (length < 0x100) {
        *out++ = 0x81;
        *out++ = (BYTE)length;
    } else {
        *out++ = 0x82;
        *out++ = (BYTE)(length >> 8);
        *out++ = (BYTE)length;
    }
    return out;
}

static DWORD der_length_size(DWORD length)
{
    return length < 0x80 ? 1 : length < 0x100 ? 2 : 3;
}

/*
 * ContentInfo of signedData: version 1, no digest algorithm, the content type data without content, the certificates
 * and no signer. the lengths are those of the parts inside each wrapper.
 */
static DWORD pkcs7_certificates(BYTE *out, PCCERT_CONTEXT *certificates, UINT count, DWORD certificates_length)
{
    static const BYTE signed_data[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02};
    static const BYTE data[] = {0x30, 0x0B, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x01};
    DWORD inner = 3 + 2 + sizeof(data) + 1 + der_length_size(certificates_length) + certificates_length + 2;
    DWORD sequence = inner;
    BYTE *start = out;
    *out++ = 0x30;
    out = der_length(out, sizeof(signed_data) + 1 + der_length_size(1 + der_length_size(sequence) + sequence) + 1 +
                              der_length_size(sequence) + sequence);
    for (UINT i = 0; i < sizeof(signed_data); i++)
        *out++ = signed_data[i];
    *out++ = 0xA0;
    out = der_length(out, 1 + der_length_size(sequence) + sequence);
    *out++ = 0x30;
    out = der_length(out, sequence);
    *out++ = 0x02;
    *out++ = 0x01;
    *out++ = 0x01;
    *out++ = 0x31;
    *out++ = 0x00;
    for (UINT i = 0; i < sizeof(data); i++)
        *out++ = data[i];
    *out++ = 0xA0;
    out = der_length(out, certificates_length);
    for (UINT i = 0; i < count; i++)
        for (DWORD j = 0; j < certificates[i]->cbCertEncoded; j++)
            *out++ = certificates[i]->pbCertEncoded[j];
    *out++ = 0x31;
    *out++ = 0x00;
    return (DWORD)(out - start);
}

BOOL WINAPI CryptUIWizExport(DWORD flags, HWND parent, LPCWSTR title, const EXPORT_INFO *info, const void *context)
{
    if (!(flags & WIZ_NO_UI))
        return no_wizard();
    if (!info || info->dwSize != sizeof(*info) || !info->pwszExportFileName || !info->pContext)
        return fail(CUI_E_INVALIDARG);
    if (info->dwSubjectChoice != EXPORT_CERT_CONTEXT)
        return fail(ERROR_NOT_SUPPORTED);
    const EXPORT_CERT_INFO *format = context;
    if (!format || format->dwSize != sizeof(*format))
        return fail(CUI_E_INVALIDARG);
    PCCERT_CONTEXT cert = info->pCertContext;

    if (format->dwExportFormat == EXPORT_DER)
        return write_file(info->pwszExportFileName, cert->pbCertEncoded, cert->cbCertEncoded);
    if (format->dwExportFormat == EXPORT_BASE64) {
        DWORD length = 0;
        if (!CUI_PROC(CryptBinaryToStringA)(cert->pbCertEncoded, cert->cbCertEncoded, CRYPT_STRING_BASE64HEADER, nullptr,
                                            &length))
            return FALSE;
        char *text = LocalAlloc(0x40, length);
        BOOL ok = text && CUI_PROC(CryptBinaryToStringA)(cert->pbCertEncoded, cert->cbCertEncoded,
                                                         CRYPT_STRING_BASE64HEADER, text, &length) &&
                  write_file(info->pwszExportFileName, text, length);
        LocalFree(text);
        return ok;
    }
    if (format->dwExportFormat == EXPORT_PKCS7) {
        /* a message of signed data that holds the certificates and nothing else. */
        HCERTSTORE memory = nullptr;
        PCCERT_CONTEXT chain[16] = {cert};
        UINT count = 1;
        if (format->fExportChain) {
            memory = CUI_PROC(CertOpenStore)(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
            if (memory && AddChainToStore(memory, cert, 0, nullptr, FALSE, nullptr)) {
                count = 0;
                for (PCCERT_CONTEXT next = CUI_PROC(CertEnumCertificatesInStore)(memory, nullptr); next && count < 16;
                     next = CUI_PROC(CertEnumCertificatesInStore)(memory, next))
                    chain[count++] = CUI_PROC(CertDuplicateCertificateContext)(next);
            }
        }
        DWORD length = 0;
        for (UINT i = 0; i < count; i++)
            length += chain[i]->cbCertEncoded;
        BYTE *message = LocalAlloc(0x40, length + 64);
        BOOL ok = message != nullptr;
        if (ok) {
            DWORD size = pkcs7_certificates(message, chain, count, length);
            ok = write_file(info->pwszExportFileName, message, size);
        }
        DWORD error = GetLastError();
        LocalFree(message);
        if (memory) {
            for (UINT i = 0; i < count; i++)
                CUI_PROC(CertFreeCertificateContext)(chain[i]);
            CUI_PROC(CertCloseStore)(memory, 0);
        }
        SetLastError(error);
        return ok;
    }
    return fail(ERROR_NOT_SUPPORTED);
}

/* ------------------------------------------------------------------ wizards and pages that need a window */

BOOL WINAPI CryptUIWizDigitalSign(DWORD flags, HWND parent, LPCWSTR title, const void *info, void **context)
{
    if (context)
        *context = nullptr;
    return no_wizard();
}

BOOL WINAPI CryptUIWizFreeDigitalSignContext(void *context)
{
    return TRUE;
}

BOOL WINAPI CryptUIWizBuildCTL(DWORD flags, HWND parent, LPCWSTR title, const void *info, const void *source,
                               const void *context)
{
    return no_wizard();
}

BOOL WINAPI CryptUIViewExpiringCerts(const void *a1, HWND parent, HCERTSTORE *stores, DWORD count, const void *a5,
                                     DWORD days)
{
    return no_wizard();
}

BOOL WINAPI IsWizardExtensionAvailable(void)
{
    return FALSE;
}

/* the pages of the property sheets that the viewers are made of, which are dialogs of Mime's own here. */
BOOL WINAPI CryptUIGetCertificatePropertiesPagesW(const void *info, BOOL *changed, void **pages, DWORD *count)
{
    if (!info || !pages || !count)
        return fail(CUI_E_INVALIDARG);
    return no_wizard();
}

BOOL WINAPI CryptUIGetCertificatePropertiesPagesA(const void *info, BOOL *changed, void **pages, DWORD *count)
{
    return CryptUIGetCertificatePropertiesPagesW(info, changed, pages, count);
}

BOOL WINAPI CryptUIGetViewSignaturesPagesW(const void *info, void **pages, DWORD *count)
{
    if (!info || !pages || !count)
        return fail(CUI_E_INVALIDARG);
    return no_wizard();
}

BOOL WINAPI CryptUIGetViewSignaturesPagesA(const void *info, void **pages, DWORD *count)
{
    return CryptUIGetViewSignaturesPagesW(info, pages, count);
}

BOOL WINAPI CryptUIFreeCertificatePropertiesPagesW(void *pages, DWORD count)
{
    LocalFree(pages);
    return TRUE;
}

BOOL WINAPI CryptUIFreeCertificatePropertiesPagesA(void *pages, DWORD count)
{
    LocalFree(pages);
    return TRUE;
}

BOOL WINAPI CryptUIFreeViewSignaturesPagesW(void *pages, DWORD count)
{
    LocalFree(pages);
    return TRUE;
}

BOOL WINAPI CryptUIFreeViewSignaturesPagesA(void *pages, DWORD count)
{
    LocalFree(pages);
    return TRUE;
}

/* ------------------------------------------------------------------ enrollment */

/* the policy servers and certification authorities are those of the enrollment services, which are not registered. */
BOOL WINAPI CryptUIDlgAddPolicyServer(HWND parent, DWORD a2, DWORD a3, void *a4, DWORD a5, DWORD a6, void *a7, void *a8,
                                      void *a9, void *a10, void *a11)
{
    return fail(CUI_REGDB_E_CLASSNOTREG);
}

BOOL WINAPI CryptUIDlgAddPolicyServerWithPriority(HWND parent, DWORD a2, DWORD a3, void *a4, DWORD a5, DWORD a6,
                                                  void *a7, void *a8, void *a9, void *a10, void *a11, void *a12)
{
    return fail(CUI_REGDB_E_CLASSNOTREG);
}

BOOL WINAPI CryptUIDlgPropertyPolicy(HWND parent, DWORD a2, DWORD a3, LPCWSTR a4, LPCWSTR a5, DWORD a6, void *a7,
                                     DWORD a8, void *a9, void *a10, void *a11)
{
    return fail(CUI_REGDB_E_CLASSNOTREG);
}

BOOL WINAPI CryptUIDlgSelectCA(void *info)
{
    return fail(CUI_REGDB_E_CLASSNOTREG);
}

BOOL WINAPI CryptUIDlgSelectPolicyServer(void *info)
{
    return fail(CUI_REGDB_E_CLASSNOTREG);
}

BOOL WINAPI CryptUIDlgFreeCAContext(void **context)
{
    if (context) {
        LocalFree(context[1]);
        LocalFree(context[2]);
        LocalFree(context);
    }
    return TRUE;
}

BOOL WINAPI CryptUIDlgFreePolicyServerContext(void **context)
{
    if (context) {
        LocalFree(context[3]);
        LocalFree(context[2]);
        LocalFree(context[1]);
        LocalFree(context);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ calls from other libraries */

/* the question whether a root certificate may be added to or taken from the protected store. */
BOOL WINAPI CertDllProtectedRootMessageBox(HWND parent, PCCERT_CONTEXT cert, DWORD action, DWORD flags)
{
    WCHAR text[1024] = {};
    CuiAppend(text, 1024, action == 1 ? L"Do you want to install this certificate?" : L"Do you want to delete this certificate?");
    CuiAppend(text, 1024, L"\r\n\r\n");
    if (cert) {
        WCHAR *name = CuiCertName(cert, FALSE);
        CuiAppend(text, 1024, L"Subject: ");
        CuiAppend(text, 1024, name);
        LocalFree(name);
    }
    return MessageBoxW(parent, text, L"Security Warning", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

/* nothing is reported to Windows Error Reporting, which is not here. */
HRESULT WINAPI CertDllLogMismatchPinRules(LPCWSTR a1, LPCWSTR a2, LPCWSTR a3, LPCWSTR a4, LPCWSTR a5, LPCWSTR a6,
                                          LPCWSTR a7)
{
    return S_OK;
}

/* the prompt of data protection needs the user interface of the credential store. */
BOOL WINAPI CryptDllProtectPrompt(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4)
{
    return fail(ERROR_NOT_SUPPORTED);
}

/* the choice of a certificate for a logon goes through the credential prompt, which answers that it is not supported. */
HRESULT WINAPI CertSelectionGetSerializedBlob(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3)
{
    if (!a1 || !a2 || !a3)
        return CUI_E_INVALIDARG;
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

/* the user interface of the Authenticode provider is that of trust, which asks the user and has no way to here. */
HRESULT WINAPI ACUIProviderInvokeUI(const void *invoke)
{
    return invoke ? E_NOTIMPL : CUI_E_INVALIDARG;
}

BOOL WINAPI CryptUIStartCertMgr(HWND parent, HINSTANCE instance, LPCSTR command_line, int show)
{
    CRYPTUI_CERT_MGR_STRUCT info = {};
    info.dwSize = sizeof(info);
    CryptUIDlgCertMgr(&info);
    return FALSE;
}
