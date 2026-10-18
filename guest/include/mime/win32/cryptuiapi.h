/* the certificate dialogs, cryptuiapi.h. */
#ifndef MIME_WIN32_CRYPTUIAPI_H
#define MIME_WIN32_CRYPTUIAPI_H

#include <mime/win32/wincrypt.h>

#ifndef CRYPTUIAPI
#define CRYPTUIAPI DECLSPEC_IMPORT
#endif

typedef struct _CRYPTUI_CERT_MGR_STRUCT {
    DWORD dwSize;
    HWND hwndParent;
    DWORD dwFlags;
    LPCWSTR pwszTitle;
    LPCSTR pszInitUsageOID;
} CRYPTUI_CERT_MGR_STRUCT, *PCRYPTUI_CERT_MGR_STRUCT;
typedef const CRYPTUI_CERT_MGR_STRUCT *PCCRYPTUI_CERT_MGR_STRUCT;

typedef struct tagCRYPTUI_VIEWCERTIFICATE_STRUCTW {
    DWORD dwSize;
    HWND hwndParent;
    DWORD dwFlags;
    LPCWSTR szTitle;
    PCCERT_CONTEXT pCertContext;
    LPCSTR *rgszPurposes;
    DWORD cPurposes;
    HANDLE hWVTStateData;
    BOOL fpCryptProviderDataTrustedUsage;
    DWORD idxSigner;
    DWORD idxCert;
    BOOL fCounterSigner;
    DWORD idxCounterSigner;
    DWORD cStores;
    HCERTSTORE *rghStores;
    DWORD cPropSheetPages;
    const void *rgPropSheetPages;
    DWORD nStartPage;
} CRYPTUI_VIEWCERTIFICATE_STRUCTW, *PCRYPTUI_VIEWCERTIFICATE_STRUCTW;
typedef const CRYPTUI_VIEWCERTIFICATE_STRUCTW *PCCRYPTUI_VIEWCERTIFICATE_STRUCTW;

CRYPTUIAPI BOOL WINAPI CryptUIDlgCertMgr(PCCRYPTUI_CERT_MGR_STRUCT info);
CRYPTUIAPI BOOL WINAPI CryptUIDlgViewCertificateW(PCCRYPTUI_VIEWCERTIFICATE_STRUCTW info, BOOL *changed);

#endif
