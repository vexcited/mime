/* cryptui internals shared by its source files. */
#ifndef MIME_CRYPTUI_PRIVATE_H
#define MIME_CRYPTUI_PRIVATE_H

#define CRYPTUIAPI
#include <mime/win32/cryptuiapi.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winnls.h>

/* the size of a structure in the layout of the 64 bit and the 32 bit Windows. */
#ifdef _WIN64
#define CUI_SIZE(x64, x86) (x64)
#else
#define CUI_SIZE(x64, x86) (x86)
#endif

#define CUI_E_INVALIDARG ((HRESULT)0x80070057L)
#define CUI_REGDB_E_CLASSNOTREG ((HRESULT)0x80040154L)
#define CUI_CRYPT_E_NOT_FOUND ((DWORD)0x80092004L)

/* the contexts of revocation and trust lists, which wincrypt.h keeps opaque. */
typedef struct {
    LPSTR pszObjId;
    CRYPT_DATA_BLOB Parameters;
} CUI_ALGORITHM;

typedef struct {
    CRYPT_DATA_BLOB SerialNumber;
    FILETIME RevocationDate;
    DWORD cExtension;
    void *rgExtension;
} CUI_CRL_ENTRY;

typedef struct {
    DWORD dwVersion;
    CUI_ALGORITHM SignatureAlgorithm;
    CRYPT_DATA_BLOB Issuer;
    FILETIME ThisUpdate, NextUpdate;
    DWORD cCRLEntry;
    CUI_CRL_ENTRY *rgCRLEntry;
    DWORD cExtension;
    void *rgExtension;
} CUI_CRL_INFO;

typedef struct {
    DWORD dwCertEncodingType;
    BYTE *pbCrlEncoded;
    DWORD cbCrlEncoded;
    CUI_CRL_INFO *pCrlInfo;
    HCERTSTORE hCertStore;
} CUI_CRL_CONTEXT;

typedef struct {
    CRYPT_DATA_BLOB SubjectIdentifier;
    DWORD cAttribute;
    void *rgAttribute;
} CUI_CTL_ENTRY;

typedef struct {
    DWORD dwVersion;
    DWORD cUsageIdentifier;
    LPSTR *rgpszUsageIdentifier;
    CRYPT_DATA_BLOB ListIdentifier, SequenceNumber;
    FILETIME ThisUpdate, NextUpdate;
    CUI_ALGORITHM SubjectAlgorithm;
    DWORD cCTLEntry;
    CUI_CTL_ENTRY *rgCTLEntry;
    DWORD cExtension;
    void *rgExtension;
} CUI_CTL_INFO;

typedef struct {
    DWORD dwMsgAndCertEncodingType;
    BYTE *pbCtlEncoded;
    DWORD cbCtlEncoded;
    CUI_CTL_INFO *pCtlInfo;
    HCERTSTORE hCertStore;
} CUI_CTL_CONTEXT;

/* crypt32 is found at run time. */
HMODULE CuiCrypt32(void);
#define CUI_PROC(function) ((__typeof__(&function))GetProcAddress(CuiCrypt32(), #function))

/* strings from LocalAlloc, for the callers to free with LocalFree. */
WCHAR *CuiCopy(const WCHAR *text);
WCHAR *CuiAnsiToWide(const char *text);
void CuiAppend(WCHAR *text, SIZE_T size, const WCHAR *more);

/* the name of a certificate: its subject or its issuer, in the simple display form. */
WCHAR *CuiCertName(PCCERT_CONTEXT cert, BOOL issuer);
/* the usages of a certificate as a list of names, or "<All>" and "<None>". */
BOOL CuiUsageString(WCHAR **out, PCCERT_CONTEXT cert, BOOL property_only);
/* the date of a file time in the layout of the user's language. */
BOOL CuiDateString(WCHAR **out, FILETIME time, BOOL with_time);

BOOL WINAPI AddChainToStore(HCERTSTORE store, PCCERT_CONTEXT cert, DWORD count, HCERTSTORE *additional,
                            BOOL skip_marked, CERT_TRUST_STATUS *status);

/* in memory dialogs. */
void CuiShowText(HWND parent, const WCHAR *title, const WCHAR *text);
/* returns the index chosen or -1. */
int CuiPickFromList(HWND parent, const WCHAR *title, const WCHAR *prompt, WCHAR **items, UINT count);

/* what the details of each kind of context read as. */
WCHAR *CuiCertDetails(PCCERT_CONTEXT cert);

#endif
