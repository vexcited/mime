#ifndef MIME_WINTRUST_H
#define MIME_WINTRUST_H
#include <mime/win32/wincrypt.h>

#define WTD_UI_ALL 1
#define WTD_UI_NONE 2
#define WTD_UI_NOBAD 3
#define WTD_UI_NOGOOD 4
#define WTD_REVOKE_NONE 0
#define WTD_REVOKE_WHOLECHAIN 1
#define WTD_CHOICE_FILE 1
#define WTD_CHOICE_CATALOG 2
#define WTD_CHOICE_BLOB 3
#define WTD_CHOICE_SIGNER 4
#define WTD_CHOICE_CERT 5
#define WTD_STATEACTION_IGNORE 0
#define WTD_STATEACTION_VERIFY 1
#define WTD_STATEACTION_CLOSE 2
#define WTD_STATEACTION_AUTO_CACHE 3
#define WTD_STATEACTION_AUTO_CACHE_FLUSH 4
#define WTD_REVOCATION_CHECK_NONE 0x10
#define WTD_REVOCATION_CHECK_END_CERT 0x20
#define WTD_REVOCATION_CHECK_CHAIN 0x40
#define WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT 0x80
#define WTD_CACHE_ONLY_URL_RETRIEVAL 0x1000
#define TRUST_E_PROVIDER_UNKNOWN ((LONG)0x800b0001)
#define TRUST_E_ACTION_UNKNOWN ((LONG)0x800b0002)
#define TRUST_E_SUBJECT_FORM_UNKNOWN ((LONG)0x800b0003)
#define TRUST_E_SUBJECT_NOT_TRUSTED ((LONG)0x800b0004)
#define TRUST_E_NOSIGNATURE ((LONG)0x800b0100)
#define TRUST_E_BAD_DIGEST ((LONG)0x80096010)
#define WINTRUST_ACTION_GENERIC_VERIFY_V2 {0x00aac56b,0xcd44,0x11d0,{0x8c,0xc2,0x00,0xc0,0x4f,0xc2,0x95,0xee}}

typedef struct WINTRUST_FILE_INFO_ {
    DWORD cbStruct;
    LPCWSTR pcwszFilePath;
    HANDLE hFile;
    GUID *pgKnownSubject;
} WINTRUST_FILE_INFO, *PWINTRUST_FILE_INFO;

typedef struct _WINTRUST_BLOB_INFO {
    DWORD cbStruct;
    GUID gSubject;
    LPCWSTR pcwszDisplayName;
    DWORD cbMemObject;
    BYTE *pbMemObject;
    DWORD cbMemSignedMsg;
    BYTE *pbMemSignedMsg;
} WINTRUST_BLOB_INFO, *PWINTRUST_BLOB_INFO;

typedef struct _WINTRUST_DATA {
    DWORD cbStruct;
    PVOID pPolicyCallbackData;
    PVOID pSIPClientData;
    DWORD dwUIChoice;
    DWORD fdwRevocationChecks;
    DWORD dwUnionChoice;
    union { WINTRUST_FILE_INFO *pFile; PVOID pCatalog; WINTRUST_BLOB_INFO *pBlob; PVOID pSgnr; PVOID pCert; };
    DWORD dwStateAction;
    HANDLE hWVTStateData;
    WCHAR *pwszURLReference;
    DWORD dwProvFlags;
    DWORD dwUIContext;
    PVOID pSignatureSettings;
} WINTRUST_DATA, *PWINTRUST_DATA;

typedef struct _CRYPT_PROVIDER_CERT {
    DWORD cbStruct;
    PCCERT_CONTEXT pCert;
    BOOL fCommercial;
    BOOL fTrustedRoot;
    BOOL fSelfSigned;
    BOOL fTestCert;
    DWORD dwRevokedReason;
    DWORD dwConfidence;
    DWORD dwError;
    PVOID pTrustListContext;
    BOOL fTrustListSignerCert;
    PVOID pCtlContext;
    DWORD dwCtlError;
    BOOL fIsCyclic;
    PCERT_CHAIN_ELEMENT pChainElement;
} CRYPT_PROVIDER_CERT, *PCRYPT_PROVIDER_CERT;

typedef struct _CRYPT_PROVIDER_SGNR {
    DWORD cbStruct;
    FILETIME sftVerifyAsOf;
    DWORD csCertChain;
    CRYPT_PROVIDER_CERT *pasCertChain;
    DWORD dwSignerType;
    PVOID psSigner;
    DWORD dwError;
    DWORD csCounterSigners;
    struct _CRYPT_PROVIDER_SGNR *pasCounterSigners;
    PCCERT_CHAIN_CONTEXT pChainContext;
} CRYPT_PROVIDER_SGNR, *PCRYPT_PROVIDER_SGNR;

typedef struct _CRYPT_PROVIDER_DATA {
    DWORD cbStruct;
    WINTRUST_DATA *pWintrustData;
    BOOL fOpenedFile;
    HWND hWndParent;
    GUID *pgActionID;
    ULONG_PTR hProv;
    DWORD dwError;
    DWORD dwRegSecuritySettings;
    DWORD dwRegPolicySettings;
    PVOID psPfns;
    DWORD cdwTrustStepErrors;
    DWORD *padwTrustStepErrors;
    DWORD chStores;
    HCERTSTORE *pahStores;
    DWORD dwEncoding;
    HCRYPTMSG hMsg;
    DWORD csSigners;
    CRYPT_PROVIDER_SGNR *pasSigners;
    DWORD csProvPrivData;
    PVOID pasProvPrivData;
    DWORD dwSubjectChoice;
    PVOID pPDSip;
    char *pszUsageOID;
    BOOL fRecallWithState;
    FILETIME sftSystemTime;
    char *pszCTLSignerUsageOID;
    DWORD dwProvFlags;
    DWORD dwFinalError;
    PCERT_USAGE_MATCH pRequestUsage;
    DWORD dwTrustPubSettings;
    DWORD dwUIStateFlags;
    PVOID pSigState;
    PVOID pSigSettings;
} CRYPT_PROVIDER_DATA, *PCRYPT_PROVIDER_DATA;

typedef HANDLE HCATADMIN;
typedef HANDLE HCATINFO;

typedef struct CATALOG_INFO_ {
    DWORD cbStruct;
    WCHAR wszCatalogFile[MAX_PATH];
} CATALOG_INFO;

typedef struct _CRYPT_TRUST_REG_ENTRY {
    DWORD cbStruct;
    WCHAR *pwszDLLName;
    WCHAR *pwszFunctionName;
} CRYPT_TRUST_REG_ENTRY;

typedef struct _CRYPT_REGISTER_ACTIONID {
    DWORD cbStruct;
    CRYPT_TRUST_REG_ENTRY sInitProvider;
    CRYPT_TRUST_REG_ENTRY sObjectProvider;
    CRYPT_TRUST_REG_ENTRY sSignatureProvider;
    CRYPT_TRUST_REG_ENTRY sCertificateProvider;
    CRYPT_TRUST_REG_ENTRY sCertificatePolicyProvider;
    CRYPT_TRUST_REG_ENTRY sFinalPolicyProvider;
    CRYPT_TRUST_REG_ENTRY sTestPolicyProvider;
    CRYPT_TRUST_REG_ENTRY sCleanupProvider;
} CRYPT_REGISTER_ACTIONID;

typedef struct _CRYPT_PROVIDER_FUNCTIONS CRYPT_PROVIDER_FUNCTIONS;

#ifdef MIME_BUILD_WINTRUST
#define WINTRUSTAPI DECLSPEC_EXPORT
#else
#define WINTRUSTAPI DECLSPEC_IMPORT
#endif
WINTRUSTAPI LONG WINAPI WinVerifyTrust(HWND window, GUID *action, PVOID data);
WINTRUSTAPI HRESULT WINAPI WinVerifyTrustEx(HWND window, GUID *action, WINTRUST_DATA *data);
WINTRUSTAPI BOOL WINAPI WTHelperCertIsSelfSigned(DWORD encoding, CERT_INFO *cert);
WINTRUSTAPI BOOL WINAPI WintrustAddActionID(GUID *action, DWORD flags, CRYPT_REGISTER_ACTIONID *info);
WINTRUSTAPI BOOL WINAPI WintrustLoadFunctionPointers(GUID *action, CRYPT_PROVIDER_FUNCTIONS *functions);
#endif
