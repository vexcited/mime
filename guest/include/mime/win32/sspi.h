/*
 * security support provider interface: secur32.dll and schannel.dll.
 *
 * secur32 implements the interface; schannel forwards its sspi exports
 * there, like both forward to sspicli.dll on windows. the declarations
 * cover sspi.h, secext.h and the lsa client calls of ntsecapi.h.
 */
#ifndef MIME_WIN32_SSPI_H
#define MIME_WIN32_SSPI_H

#include <mime/win32/winbase.h>

#undef SECURITYAPI
#ifdef MIME_BUILD_SECUR32
#define SECURITYAPI DECLSPEC_EXPORT
#else
#define SECURITYAPI DECLSPEC_IMPORT
#endif

#define SEC_ENTRY WINAPI

typedef LONG SECURITY_STATUS;
typedef WCHAR SEC_WCHAR;
typedef CHAR SEC_CHAR;
typedef LARGE_INTEGER SECURITY_INTEGER, *PSECURITY_INTEGER;
typedef SECURITY_INTEGER TimeStamp, *PTimeStamp;

/* ------------------------------------------------------------------ status codes */

#define SEC_E_OK ((SECURITY_STATUS)0x00000000L)
#define SEC_E_INSUFFICIENT_MEMORY ((SECURITY_STATUS)0x80090300L)
#define SEC_E_INVALID_HANDLE ((SECURITY_STATUS)0x80090301L)
#define SEC_E_UNSUPPORTED_FUNCTION ((SECURITY_STATUS)0x80090302L)
#define SEC_E_TARGET_UNKNOWN ((SECURITY_STATUS)0x80090303L)
#define SEC_E_INTERNAL_ERROR ((SECURITY_STATUS)0x80090304L)
#define SEC_E_SECPKG_NOT_FOUND ((SECURITY_STATUS)0x80090305L)
#define SEC_E_NOT_OWNER ((SECURITY_STATUS)0x80090306L)
#define SEC_E_CANNOT_INSTALL ((SECURITY_STATUS)0x80090307L)
#define SEC_E_INVALID_TOKEN ((SECURITY_STATUS)0x80090308L)
#define SEC_E_CANNOT_PACK ((SECURITY_STATUS)0x80090309L)
#define SEC_E_QOP_NOT_SUPPORTED ((SECURITY_STATUS)0x8009030AL)
#define SEC_E_NO_IMPERSONATION ((SECURITY_STATUS)0x8009030BL)
#define SEC_E_LOGON_DENIED ((SECURITY_STATUS)0x8009030CL)
#define SEC_E_UNKNOWN_CREDENTIALS ((SECURITY_STATUS)0x8009030DL)
#define SEC_E_NO_CREDENTIALS ((SECURITY_STATUS)0x8009030EL)
#define SEC_E_MESSAGE_ALTERED ((SECURITY_STATUS)0x8009030FL)
#define SEC_E_OUT_OF_SEQUENCE ((SECURITY_STATUS)0x80090310L)
#define SEC_E_NO_AUTHENTICATING_AUTHORITY ((SECURITY_STATUS)0x80090311L)
#define SEC_I_CONTINUE_NEEDED ((SECURITY_STATUS)0x00090312L)
#define SEC_I_COMPLETE_NEEDED ((SECURITY_STATUS)0x00090313L)
#define SEC_I_COMPLETE_AND_CONTINUE ((SECURITY_STATUS)0x00090314L)
#define SEC_I_LOCAL_LOGON ((SECURITY_STATUS)0x00090315L)
#define SEC_E_BAD_PKGID ((SECURITY_STATUS)0x80090316L)
#define SEC_E_CONTEXT_EXPIRED ((SECURITY_STATUS)0x80090317L)
#define SEC_I_CONTEXT_EXPIRED ((SECURITY_STATUS)0x00090317L)
#define SEC_E_INCOMPLETE_MESSAGE ((SECURITY_STATUS)0x80090318L)
#define SEC_E_INCOMPLETE_CREDENTIALS ((SECURITY_STATUS)0x80090320L)
#define SEC_E_BUFFER_TOO_SMALL ((SECURITY_STATUS)0x80090321L)
#define SEC_I_INCOMPLETE_CREDENTIALS ((SECURITY_STATUS)0x00090320L)
#define SEC_I_RENEGOTIATE ((SECURITY_STATUS)0x00090321L)
#define SEC_E_WRONG_PRINCIPAL ((SECURITY_STATUS)0x80090322L)
#define SEC_E_TIME_SKEW ((SECURITY_STATUS)0x80090324L)
#define SEC_E_UNTRUSTED_ROOT ((SECURITY_STATUS)0x80090325L)
#define SEC_E_ILLEGAL_MESSAGE ((SECURITY_STATUS)0x80090326L)
#define SEC_E_CERT_UNKNOWN ((SECURITY_STATUS)0x80090327L)
#define SEC_E_CERT_EXPIRED ((SECURITY_STATUS)0x80090328L)
#define SEC_E_ENCRYPT_FAILURE ((SECURITY_STATUS)0x80090329L)
#define SEC_E_DECRYPT_FAILURE ((SECURITY_STATUS)0x80090330L)
#define SEC_E_ALGORITHM_MISMATCH ((SECURITY_STATUS)0x80090331L)
#define SEC_E_UNFINISHED_CONTEXT_DELETED ((SECURITY_STATUS)0x80090333L)
#define SEC_E_INVALID_PARAMETER ((SECURITY_STATUS)0x8009035DL)

/* ------------------------------------------------------------------ handles and buffers */

typedef struct _SecHandle {
    ULONG_PTR dwLower;
    ULONG_PTR dwUpper;
} SecHandle, *PSecHandle;

typedef SecHandle CredHandle, *PCredHandle;
typedef SecHandle CtxtHandle, *PCtxtHandle;

#define SecInvalidateHandle(x) ((PSecHandle)(x))->dwLower = ((PSecHandle)(x))->dwUpper = ((ULONG_PTR)((INT_PTR)-1))
#define SecIsValidHandle(x)                                                                                            \
    ((((PSecHandle)(x))->dwLower != ((ULONG_PTR)((INT_PTR)-1))) &&                                                     \
     (((PSecHandle)(x))->dwUpper != ((ULONG_PTR)((INT_PTR)-1))))

typedef struct _SecBuffer {
    ULONG cbBuffer;
    ULONG BufferType;
    void *pvBuffer;
} SecBuffer, *PSecBuffer;

typedef struct _SecBufferDesc {
    ULONG ulVersion;
    ULONG cBuffers;
    PSecBuffer pBuffers;
} SecBufferDesc, *PSecBufferDesc;

#define SECBUFFER_VERSION 0
#define SECBUFFER_EMPTY 0
#define SECBUFFER_DATA 1
#define SECBUFFER_TOKEN 2
#define SECBUFFER_PKG_PARAMS 3
#define SECBUFFER_MISSING 4
#define SECBUFFER_EXTRA 5
#define SECBUFFER_STREAM_TRAILER 6
#define SECBUFFER_STREAM_HEADER 7
#define SECBUFFER_ATTRMASK 0xF0000000

/* ------------------------------------------------------------------ packages */

typedef struct _SecPkgInfoW {
    ULONG fCapabilities;
    USHORT wVersion;
    USHORT wRPCID;
    ULONG cbMaxToken;
    SEC_WCHAR *Name;
    SEC_WCHAR *Comment;
} SecPkgInfoW, *PSecPkgInfoW;

typedef struct _SecPkgInfoA {
    ULONG fCapabilities;
    USHORT wVersion;
    USHORT wRPCID;
    ULONG cbMaxToken;
    SEC_CHAR *Name;
    SEC_CHAR *Comment;
} SecPkgInfoA, *PSecPkgInfoA;

#define SECPKG_FLAG_INTEGRITY 0x00000001
#define SECPKG_FLAG_PRIVACY 0x00000002
#define SECPKG_FLAG_TOKEN_ONLY 0x00000004
#define SECPKG_FLAG_DATAGRAM 0x00000008
#define SECPKG_FLAG_CONNECTION 0x00000010
#define SECPKG_FLAG_MULTI_REQUIRED 0x00000020
#define SECPKG_FLAG_CLIENT_ONLY 0x00000040
#define SECPKG_FLAG_EXTENDED_ERROR 0x00000080
#define SECPKG_FLAG_IMPERSONATION 0x00000100
#define SECPKG_FLAG_ACCEPT_WIN32_NAME 0x00000200
#define SECPKG_FLAG_STREAM 0x00000400
#define SECPKG_FLAG_NEGOTIABLE 0x00000800
#define SECPKG_FLAG_GSS_COMPATIBLE 0x00001000
#define SECPKG_FLAG_LOGON 0x00002000
#define SECPKG_FLAG_ASCII_BUFFERS 0x00004000
#define SECPKG_FLAG_FRAGMENT 0x00008000
#define SECPKG_FLAG_MUTUAL_AUTH 0x00010000
#define SECPKG_FLAG_DELEGATION 0x00020000
#define SECPKG_FLAG_READONLY_WITH_CHECKSUM 0x00040000
#define SECPKG_FLAG_RESTRICTED_TOKENS 0x00080000
#define SECPKG_FLAG_NEGO_EXTENDER 0x00100000
#define SECPKG_FLAG_NEGOTIABLE2 0x00200000
#define SECPKG_FLAG_APPCONTAINER_PASSTHROUGH 0x00400000
#define SECPKG_FLAG_APPCONTAINER_CHECKS 0x00800000
#define SECPKG_FLAG_CREDENTIAL_ISOLATION_ENABLED 0x01000000
#define SECPKG_FLAG_APPLY_LOOPBACK 0x02000000

#define SECPKG_ID_NONE 0xFFFF

#define NEGOSSP_NAME_W L"Negotiate"
#define NEGOSSP_NAME_A "Negotiate"
#define NTLMSP_NAME_W L"NTLM"
#define NTLMSP_NAME_A "NTLM"
#define MICROSOFT_KERBEROS_NAME_W L"Kerberos"
#define MICROSOFT_KERBEROS_NAME_A "Kerberos"
#define UNISP_NAME_W L"Microsoft Unified Security Protocol Provider"
#define UNISP_NAME_A "Microsoft Unified Security Protocol Provider"
#define SCHANNEL_NAME_W L"Schannel"
#define SCHANNEL_NAME_A "Schannel"

/* ------------------------------------------------------------------ credentials */

#define SECPKG_CRED_INBOUND 0x00000001
#define SECPKG_CRED_OUTBOUND 0x00000002
#define SECPKG_CRED_BOTH 0x00000003
#define SECPKG_CRED_DEFAULT 0x00000004
#define SECPKG_CRED_RESERVED 0xF0000000
#define SECPKG_CRED_AUTOLOGON_RESTRICTED 0x00000010
#define SECPKG_CRED_PROCESS_POLICY_ONLY 0x00000020

#define SECPKG_CRED_ATTR_NAMES 1
#define SECPKG_CRED_ATTR_SSI_PROVIDER 2
#define SECPKG_CRED_ATTR_KDC_PROXY_SETTINGS 3
#define SECPKG_CRED_ATTR_CERT 4
#define SECPKG_CRED_ATTR_PAC_BYPASS 5

typedef struct _SecPkgCredentials_NamesW {
    SEC_WCHAR *sUserName;
} SecPkgCredentials_NamesW, *PSecPkgCredentials_NamesW;

typedef struct _SecPkgCredentials_NamesA {
    SEC_CHAR *sUserName;
} SecPkgCredentials_NamesA, *PSecPkgCredentials_NamesA;

typedef void(SEC_ENTRY *SEC_GET_KEY_FN)(void *arg, void *principal, ULONG key_version, void **key, SECURITY_STATUS *status);

#define SCH_CRED_V3 3
#define SCHANNEL_CRED_VERSION 4
#define SCH_CREDENTIALS_VERSION 5

/* the identity structures passed as authentication data. */
#define SEC_WINNT_AUTH_IDENTITY_ANSI 0x1
#define SEC_WINNT_AUTH_IDENTITY_UNICODE 0x2
#define SEC_WINNT_AUTH_IDENTITY_VERSION 0x200
#define SEC_WINNT_AUTH_IDENTITY_VERSION_2 0x201
#define SEC_WINNT_AUTH_IDENTITY_MARSHALLED 0x4
#define SEC_WINNT_AUTH_IDENTITY_ONLY 0x8
#define SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED 0x10
#define SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_PROTECTED 0x20
#define SEC_WINNT_AUTH_IDENTITY_FLAGS_USER_PROTECTED 0x40
#define SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_ENCRYPTED 0x80

typedef struct _SEC_WINNT_AUTH_IDENTITY_W {
    USHORT *User;
    ULONG UserLength;
    USHORT *Domain;
    ULONG DomainLength;
    USHORT *Password;
    ULONG PasswordLength;
    ULONG Flags;
} SEC_WINNT_AUTH_IDENTITY_W, *PSEC_WINNT_AUTH_IDENTITY_W;

typedef struct _SEC_WINNT_AUTH_IDENTITY_A {
    UCHAR *User;
    ULONG UserLength;
    UCHAR *Domain;
    ULONG DomainLength;
    UCHAR *Password;
    ULONG PasswordLength;
    ULONG Flags;
} SEC_WINNT_AUTH_IDENTITY_A, *PSEC_WINNT_AUTH_IDENTITY_A;

typedef struct _SEC_WINNT_AUTH_IDENTITY_EXW {
    ULONG Version;
    ULONG Length;
    USHORT *User;
    ULONG UserLength;
    USHORT *Domain;
    ULONG DomainLength;
    USHORT *Password;
    ULONG PasswordLength;
    ULONG Flags;
    USHORT *PackageList;
    ULONG PackageListLength;
} SEC_WINNT_AUTH_IDENTITY_EXW, *PSEC_WINNT_AUTH_IDENTITY_EXW;

typedef struct _SEC_WINNT_AUTH_IDENTITY_EX2 {
    ULONG Version;
    USHORT cbHeaderLength;
    ULONG cbStructureLength;
    ULONG UserOffset;
    USHORT UserLength;
    ULONG DomainOffset;
    USHORT DomainLength;
    ULONG PackedCredentialsOffset;
    USHORT PackedCredentialsLength;
    ULONG Flags;
    ULONG PackageListOffset;
    USHORT PackageListLength;
} SEC_WINNT_AUTH_IDENTITY_EX2, *PSEC_WINNT_AUTH_IDENTITY_EX2;

typedef PVOID PSEC_WINNT_AUTH_IDENTITY_OPAQUE;

/* ------------------------------------------------------------------ contexts */

#define SECPKG_ATTR_SIZES 0
#define SECPKG_ATTR_NAMES 1
#define SECPKG_ATTR_LIFESPAN 2
#define SECPKG_ATTR_DCE_INFO 3
#define SECPKG_ATTR_STREAM_SIZES 4
#define SECPKG_ATTR_KEY_INFO 5
#define SECPKG_ATTR_AUTHORITY 6
#define SECPKG_ATTR_PACKAGE_INFO 10
#define SECPKG_ATTR_NEGOTIATION_INFO 12
#define SECPKG_ATTR_REMOTE_CERT_CONTEXT 0x53
#define SECPKG_ATTR_CONNECTION_INFO 0x5A
#define SECPKG_ATTR_SUPPORTED_ALGS 86
#define SECPKG_ATTR_CIPHER_STRENGTHS 87
#define SECPKG_ATTR_SUPPORTED_PROTOCOLS 88

#define SECURITY_NATIVE_DREP 0x00000010
#define SECURITY_NETWORK_DREP 0x00000000

#define SECPKG_CONTEXT_EXPORT_RESET_NEW 0x00000001
#define SECPKG_CONTEXT_EXPORT_DELETE_OLD 0x00000002

/* ------------------------------------------------------------------ the dispatch table */

typedef SECURITY_STATUS(SEC_ENTRY *ENUMERATE_SECURITY_PACKAGES_FN_W)(ULONG *, PSecPkgInfoW *);
typedef SECURITY_STATUS(SEC_ENTRY *ENUMERATE_SECURITY_PACKAGES_FN_A)(ULONG *, PSecPkgInfoA *);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CREDENTIALS_ATTRIBUTES_FN_W)(PCredHandle, ULONG, void *);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CREDENTIALS_ATTRIBUTES_FN_A)(PCredHandle, ULONG, void *);
typedef SECURITY_STATUS(SEC_ENTRY *ACQUIRE_CREDENTIALS_HANDLE_FN_W)(SEC_WCHAR *, SEC_WCHAR *, ULONG, void *, void *,
                                                                     SEC_GET_KEY_FN, void *, PCredHandle, PTimeStamp);
typedef SECURITY_STATUS(SEC_ENTRY *ACQUIRE_CREDENTIALS_HANDLE_FN_A)(SEC_CHAR *, SEC_CHAR *, ULONG, void *, void *,
                                                                     SEC_GET_KEY_FN, void *, PCredHandle, PTimeStamp);
typedef SECURITY_STATUS(SEC_ENTRY *FREE_CREDENTIALS_HANDLE_FN)(PCredHandle);
typedef SECURITY_STATUS(SEC_ENTRY *INITIALIZE_SECURITY_CONTEXT_FN_W)(PCredHandle, PCtxtHandle, SEC_WCHAR *, ULONG, ULONG,
                                                                      ULONG, PSecBufferDesc, ULONG, PCtxtHandle,
                                                                      PSecBufferDesc, ULONG *, PTimeStamp);
typedef SECURITY_STATUS(SEC_ENTRY *INITIALIZE_SECURITY_CONTEXT_FN_A)(PCredHandle, PCtxtHandle, SEC_CHAR *, ULONG, ULONG,
                                                                      ULONG, PSecBufferDesc, ULONG, PCtxtHandle,
                                                                      PSecBufferDesc, ULONG *, PTimeStamp);
typedef SECURITY_STATUS(SEC_ENTRY *ACCEPT_SECURITY_CONTEXT_FN)(PCredHandle, PCtxtHandle, PSecBufferDesc, ULONG, ULONG,
                                                                PCtxtHandle, PSecBufferDesc, ULONG *, PTimeStamp);
typedef SECURITY_STATUS(SEC_ENTRY *COMPLETE_AUTH_TOKEN_FN)(PCtxtHandle, PSecBufferDesc);
typedef SECURITY_STATUS(SEC_ENTRY *DELETE_SECURITY_CONTEXT_FN)(PCtxtHandle);
typedef SECURITY_STATUS(SEC_ENTRY *APPLY_CONTROL_TOKEN_FN)(PCtxtHandle, PSecBufferDesc);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CONTEXT_ATTRIBUTES_FN_W)(PCtxtHandle, ULONG, void *);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CONTEXT_ATTRIBUTES_FN_A)(PCtxtHandle, ULONG, void *);
typedef SECURITY_STATUS(SEC_ENTRY *IMPERSONATE_SECURITY_CONTEXT_FN)(PCtxtHandle);
typedef SECURITY_STATUS(SEC_ENTRY *REVERT_SECURITY_CONTEXT_FN)(PCtxtHandle);
typedef SECURITY_STATUS(SEC_ENTRY *MAKE_SIGNATURE_FN)(PCtxtHandle, ULONG, PSecBufferDesc, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *VERIFY_SIGNATURE_FN)(PCtxtHandle, PSecBufferDesc, ULONG, ULONG *);
typedef SECURITY_STATUS(SEC_ENTRY *FREE_CONTEXT_BUFFER_FN)(void *);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_SECURITY_PACKAGE_INFO_FN_W)(SEC_WCHAR *, PSecPkgInfoW *);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_SECURITY_PACKAGE_INFO_FN_A)(SEC_CHAR *, PSecPkgInfoA *);
typedef SECURITY_STATUS(SEC_ENTRY *EXPORT_SECURITY_CONTEXT_FN)(PCtxtHandle, ULONG, PSecBuffer, void **);
typedef SECURITY_STATUS(SEC_ENTRY *IMPORT_SECURITY_CONTEXT_FN_W)(SEC_WCHAR *, PSecBuffer, void *, PCtxtHandle);
typedef SECURITY_STATUS(SEC_ENTRY *IMPORT_SECURITY_CONTEXT_FN_A)(SEC_CHAR *, PSecBuffer, void *, PCtxtHandle);
typedef SECURITY_STATUS(SEC_ENTRY *ADD_CREDENTIALS_FN_W)(PCredHandle, SEC_WCHAR *, SEC_WCHAR *, ULONG, void *,
                                                          SEC_GET_KEY_FN, void *, PTimeStamp);
typedef SECURITY_STATUS(SEC_ENTRY *ADD_CREDENTIALS_FN_A)(PCredHandle, SEC_CHAR *, SEC_CHAR *, ULONG, void *,
                                                          SEC_GET_KEY_FN, void *, PTimeStamp);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_SECURITY_CONTEXT_TOKEN_FN)(PCtxtHandle, void **);
typedef SECURITY_STATUS(SEC_ENTRY *ENCRYPT_MESSAGE_FN)(PCtxtHandle, ULONG, PSecBufferDesc, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *DECRYPT_MESSAGE_FN)(PCtxtHandle, PSecBufferDesc, ULONG, ULONG *);
typedef SECURITY_STATUS(SEC_ENTRY *SET_CONTEXT_ATTRIBUTES_FN_W)(PCtxtHandle, ULONG, void *, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *SET_CONTEXT_ATTRIBUTES_FN_A)(PCtxtHandle, ULONG, void *, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *SET_CREDENTIALS_ATTRIBUTES_FN_W)(PCredHandle, ULONG, void *, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *SET_CREDENTIALS_ATTRIBUTES_FN_A)(PCredHandle, ULONG, void *, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *CHANGE_PASSWORD_FN_W)(SEC_WCHAR *, SEC_WCHAR *, SEC_WCHAR *, SEC_WCHAR *, SEC_WCHAR *,
                                                          BOOLEAN, ULONG, PSecBufferDesc);
typedef SECURITY_STATUS(SEC_ENTRY *CHANGE_PASSWORD_FN_A)(SEC_CHAR *, SEC_CHAR *, SEC_CHAR *, SEC_CHAR *, SEC_CHAR *,
                                                          BOOLEAN, ULONG, PSecBufferDesc);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CONTEXT_ATTRIBUTES_EX_FN_W)(PCtxtHandle, ULONG, void *, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CONTEXT_ATTRIBUTES_EX_FN_A)(PCtxtHandle, ULONG, void *, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CREDENTIALS_ATTRIBUTES_EX_FN_W)(PCredHandle, ULONG, void *, ULONG);
typedef SECURITY_STATUS(SEC_ENTRY *QUERY_CREDENTIALS_ATTRIBUTES_EX_FN_A)(PCredHandle, ULONG, void *, ULONG);

#define SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION 1
#define SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION_2 2
#define SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION_3 3
#define SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION_4 4
#define SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION_5 5

typedef struct _SECURITY_FUNCTION_TABLE_W {
    ULONG dwVersion;
    ENUMERATE_SECURITY_PACKAGES_FN_W EnumerateSecurityPackagesW;
    QUERY_CREDENTIALS_ATTRIBUTES_FN_W QueryCredentialsAttributesW;
    ACQUIRE_CREDENTIALS_HANDLE_FN_W AcquireCredentialsHandleW;
    FREE_CREDENTIALS_HANDLE_FN FreeCredentialsHandle;
    void *Reserved2;
    INITIALIZE_SECURITY_CONTEXT_FN_W InitializeSecurityContextW;
    ACCEPT_SECURITY_CONTEXT_FN AcceptSecurityContext;
    COMPLETE_AUTH_TOKEN_FN CompleteAuthToken;
    DELETE_SECURITY_CONTEXT_FN DeleteSecurityContext;
    APPLY_CONTROL_TOKEN_FN ApplyControlToken;
    QUERY_CONTEXT_ATTRIBUTES_FN_W QueryContextAttributesW;
    IMPERSONATE_SECURITY_CONTEXT_FN ImpersonateSecurityContext;
    REVERT_SECURITY_CONTEXT_FN RevertSecurityContext;
    MAKE_SIGNATURE_FN MakeSignature;
    VERIFY_SIGNATURE_FN VerifySignature;
    FREE_CONTEXT_BUFFER_FN FreeContextBuffer;
    QUERY_SECURITY_PACKAGE_INFO_FN_W QuerySecurityPackageInfoW;
    void *Reserved3;
    void *Reserved4;
    EXPORT_SECURITY_CONTEXT_FN ExportSecurityContext;
    IMPORT_SECURITY_CONTEXT_FN_W ImportSecurityContextW;
    ADD_CREDENTIALS_FN_W AddCredentialsW;
    void *Reserved8;
    QUERY_SECURITY_CONTEXT_TOKEN_FN QuerySecurityContextToken;
    ENCRYPT_MESSAGE_FN EncryptMessage;
    DECRYPT_MESSAGE_FN DecryptMessage;
    SET_CONTEXT_ATTRIBUTES_FN_W SetContextAttributesW;
    SET_CREDENTIALS_ATTRIBUTES_FN_W SetCredentialsAttributesW;
    CHANGE_PASSWORD_FN_W ChangeAccountPasswordW;
    QUERY_CONTEXT_ATTRIBUTES_EX_FN_W QueryContextAttributesExW;
    QUERY_CREDENTIALS_ATTRIBUTES_EX_FN_W QueryCredentialsAttributesExW;
} SecurityFunctionTableW, *PSecurityFunctionTableW;

typedef struct _SECURITY_FUNCTION_TABLE_A {
    ULONG dwVersion;
    ENUMERATE_SECURITY_PACKAGES_FN_A EnumerateSecurityPackagesA;
    QUERY_CREDENTIALS_ATTRIBUTES_FN_A QueryCredentialsAttributesA;
    ACQUIRE_CREDENTIALS_HANDLE_FN_A AcquireCredentialsHandleA;
    FREE_CREDENTIALS_HANDLE_FN FreeCredentialsHandle;
    void *Reserved2;
    INITIALIZE_SECURITY_CONTEXT_FN_A InitializeSecurityContextA;
    ACCEPT_SECURITY_CONTEXT_FN AcceptSecurityContext;
    COMPLETE_AUTH_TOKEN_FN CompleteAuthToken;
    DELETE_SECURITY_CONTEXT_FN DeleteSecurityContext;
    APPLY_CONTROL_TOKEN_FN ApplyControlToken;
    QUERY_CONTEXT_ATTRIBUTES_FN_A QueryContextAttributesA;
    IMPERSONATE_SECURITY_CONTEXT_FN ImpersonateSecurityContext;
    REVERT_SECURITY_CONTEXT_FN RevertSecurityContext;
    MAKE_SIGNATURE_FN MakeSignature;
    VERIFY_SIGNATURE_FN VerifySignature;
    FREE_CONTEXT_BUFFER_FN FreeContextBuffer;
    QUERY_SECURITY_PACKAGE_INFO_FN_A QuerySecurityPackageInfoA;
    void *Reserved3;
    void *Reserved4;
    EXPORT_SECURITY_CONTEXT_FN ExportSecurityContext;
    IMPORT_SECURITY_CONTEXT_FN_A ImportSecurityContextA;
    ADD_CREDENTIALS_FN_A AddCredentialsA;
    void *Reserved8;
    QUERY_SECURITY_CONTEXT_TOKEN_FN QuerySecurityContextToken;
    ENCRYPT_MESSAGE_FN EncryptMessage;
    DECRYPT_MESSAGE_FN DecryptMessage;
    SET_CONTEXT_ATTRIBUTES_FN_A SetContextAttributesA;
    SET_CREDENTIALS_ATTRIBUTES_FN_A SetCredentialsAttributesA;
    CHANGE_PASSWORD_FN_A ChangeAccountPasswordA;
    QUERY_CONTEXT_ATTRIBUTES_EX_FN_A QueryContextAttributesExA;
    QUERY_CREDENTIALS_ATTRIBUTES_EX_FN_A QueryCredentialsAttributesExA;
} SecurityFunctionTableA, *PSecurityFunctionTableA;

MIME_STATIC_ASSERT(sizeof(SecPkgInfoW) == (sizeof(void *) == 4 ? 20 : 32), "SecPkgInfoW size");
MIME_FIELD_AT(SecurityFunctionTableW, QuerySecurityContextToken, 24 * sizeof(void *));
MIME_FIELD_AT(SecurityFunctionTableW, SetContextAttributesW, 27 * sizeof(void *));
MIME_FIELD_AT(SEC_WINNT_AUTH_IDENTITY_EX2, Flags, 36);

typedef struct _SECURITY_PACKAGE_OPTIONS {
    ULONG Size;
    ULONG Type;
    ULONG Flags;
    ULONG SignatureSize;
    void *Signature;
} SECURITY_PACKAGE_OPTIONS, *PSECURITY_PACKAGE_OPTIONS;

/* ------------------------------------------------------------------ names, secext.h */

typedef enum {
    NameUnknown = 0,
    NameFullyQualifiedDN = 1,
    NameSamCompatible = 2,
    NameDisplay = 3,
    NameUniqueId = 6,
    NameCanonical = 7,
    NameUserPrincipal = 8,
    NameCanonicalEx = 9,
    NameServicePrincipal = 10,
    NameDnsDomain = 12,
    NameGivenName = 13,
    NameSurname = 14
} EXTENDED_NAME_FORMAT, *PEXTENDED_NAME_FORMAT;

/* ------------------------------------------------------------------ lsa client, ntsecapi.h */

typedef STRING LSA_STRING, *PLSA_STRING;
typedef UNICODE_STRING LSA_UNICODE_STRING, *PLSA_UNICODE_STRING;
typedef ULONG LSA_OPERATIONAL_MODE, *PLSA_OPERATIONAL_MODE;

typedef enum _SECURITY_LOGON_TYPE {
    UndefinedLogonType = 0,
    Interactive = 2,
    Network,
    Batch,
    Service,
    Proxy,
    Unlock,
    NetworkCleartext,
    NewCredentials,
    RemoteInteractive,
    CachedInteractive,
    CachedRemoteInteractive,
    CachedUnlock
} SECURITY_LOGON_TYPE, *PSECURITY_LOGON_TYPE;

typedef enum _POLICY_NOTIFICATION_INFORMATION_CLASS {
    PolicyNotifyAuditEventsInformation = 1,
    PolicyNotifyAccountDomainInformation,
    PolicyNotifyServerRoleInformation,
    PolicyNotifyDnsDomainInformation,
    PolicyNotifyDomainEfsInformation,
    PolicyNotifyDomainKerberosTicketInformation,
    PolicyNotifyMachineAccountPasswordInformation,
    PolicyNotifyGlobalSaclInformation,
    PolicyNotifyMax
} POLICY_NOTIFICATION_INFORMATION_CLASS, *PPOLICY_NOTIFICATION_INFORMATION_CLASS;

/* the logon identifiers below point to a LUID: two 32 bit halves, low part first. */
typedef void *PSSPI_LUID;

/* ------------------------------------------------------------------ secur32 exports */

SECURITYAPI SECURITY_STATUS SEC_ENTRY EnumerateSecurityPackagesW(ULONG *count, PSecPkgInfoW *info);
SECURITYAPI SECURITY_STATUS SEC_ENTRY EnumerateSecurityPackagesA(ULONG *count, PSecPkgInfoA *info);
SECURITYAPI SECURITY_STATUS SEC_ENTRY QuerySecurityPackageInfoW(SEC_WCHAR *name, PSecPkgInfoW *info);
SECURITYAPI SECURITY_STATUS SEC_ENTRY QuerySecurityPackageInfoA(SEC_CHAR *name, PSecPkgInfoA *info);
SECURITYAPI SECURITY_STATUS SEC_ENTRY FreeContextBuffer(void *buffer);
SECURITYAPI PSecurityFunctionTableW SEC_ENTRY InitSecurityInterfaceW(void);
SECURITYAPI PSecurityFunctionTableA SEC_ENTRY InitSecurityInterfaceA(void);
SECURITYAPI SECURITY_STATUS SEC_ENTRY AcquireCredentialsHandleW(SEC_WCHAR *principal, SEC_WCHAR *package, ULONG use,
                                                                void *logon_id, void *auth_data, SEC_GET_KEY_FN get_key,
                                                                void *get_key_argument, PCredHandle credential,
                                                                PTimeStamp expiry);
SECURITYAPI SECURITY_STATUS SEC_ENTRY AcquireCredentialsHandleA(SEC_CHAR *principal, SEC_CHAR *package, ULONG use,
                                                                void *logon_id, void *auth_data, SEC_GET_KEY_FN get_key,
                                                                void *get_key_argument, PCredHandle credential,
                                                                PTimeStamp expiry);
SECURITYAPI SECURITY_STATUS SEC_ENTRY FreeCredentialsHandle(PCredHandle credential);
SECURITYAPI SECURITY_STATUS SEC_ENTRY InitializeSecurityContextW(PCredHandle credential, PCtxtHandle context,
                                                                 SEC_WCHAR *target, ULONG flags, ULONG reserved1,
                                                                 ULONG representation, PSecBufferDesc input,
                                                                 ULONG reserved2, PCtxtHandle new_context,
                                                                 PSecBufferDesc output, ULONG *attributes,
                                                                 PTimeStamp expiry);
SECURITYAPI SECURITY_STATUS SEC_ENTRY InitializeSecurityContextA(PCredHandle credential, PCtxtHandle context,
                                                                 SEC_CHAR *target, ULONG flags, ULONG reserved1,
                                                                 ULONG representation, PSecBufferDesc input,
                                                                 ULONG reserved2, PCtxtHandle new_context,
                                                                 PSecBufferDesc output, ULONG *attributes,
                                                                 PTimeStamp expiry);
SECURITYAPI SECURITY_STATUS SEC_ENTRY AcceptSecurityContext(PCredHandle credential, PCtxtHandle context,
                                                            PSecBufferDesc input, ULONG flags, ULONG representation,
                                                            PCtxtHandle new_context, PSecBufferDesc output,
                                                            ULONG *attributes, PTimeStamp expiry);
SECURITYAPI SECURITY_STATUS SEC_ENTRY DeleteSecurityContext(PCtxtHandle context);
SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryContextAttributesW(PCtxtHandle context, ULONG attribute, void *buffer);
SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryContextAttributesA(PCtxtHandle context, ULONG attribute, void *buffer);
SECURITYAPI SECURITY_STATUS SEC_ENTRY EncryptMessage(PCtxtHandle context, ULONG qop, PSecBufferDesc message,
                                                     ULONG sequence);
SECURITYAPI SECURITY_STATUS SEC_ENTRY DecryptMessage(PCtxtHandle context, PSecBufferDesc message, ULONG sequence,
                                                     ULONG *qop);
SECURITYAPI SECURITY_STATUS SEC_ENTRY CompleteAuthToken(PCtxtHandle context, PSecBufferDesc token);
SECURITYAPI BOOLEAN SEC_ENTRY GetUserNameExW(EXTENDED_NAME_FORMAT format, LPWSTR buffer, PULONG size);
SECURITYAPI BOOLEAN SEC_ENTRY GetUserNameExA(EXTENDED_NAME_FORMAT format, LPSTR buffer, PULONG size);

SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryCredentialsAttributesW(PCredHandle credential, ULONG attribute, void *buffer);
SECURITYAPI SECURITY_STATUS SEC_ENTRY QueryCredentialsAttributesA(PCredHandle credential, ULONG attribute, void *buffer);
SECURITYAPI SECURITY_STATUS SEC_ENTRY SetCredentialsAttributesW(PCredHandle credential, ULONG attribute, void *buffer,
                                                                ULONG size);
SECURITYAPI SECURITY_STATUS SEC_ENTRY SetCredentialsAttributesA(PCredHandle credential, ULONG attribute, void *buffer,
                                                                ULONG size);
SECURITYAPI SECURITY_STATUS SEC_ENTRY AddCredentialsW(PCredHandle credential, SEC_WCHAR *principal, SEC_WCHAR *package,
                                                      ULONG use, void *auth_data, SEC_GET_KEY_FN get_key,
                                                      void *get_key_argument, PTimeStamp expiry);
SECURITYAPI SECURITY_STATUS SEC_ENTRY AddCredentialsA(PCredHandle credential, SEC_CHAR *principal, SEC_CHAR *package,
                                                      ULONG use, void *auth_data, SEC_GET_KEY_FN get_key,
                                                      void *get_key_argument, PTimeStamp expiry);
SECURITYAPI SECURITY_STATUS SEC_ENTRY SetContextAttributesW(PCtxtHandle context, ULONG attribute, void *buffer,
                                                            ULONG size);
SECURITYAPI SECURITY_STATUS SEC_ENTRY SetContextAttributesA(PCtxtHandle context, ULONG attribute, void *buffer,
                                                            ULONG size);
SECURITYAPI SECURITY_STATUS SEC_ENTRY ApplyControlToken(PCtxtHandle context, PSecBufferDesc input);
SECURITYAPI SECURITY_STATUS SEC_ENTRY ImpersonateSecurityContext(PCtxtHandle context);
SECURITYAPI SECURITY_STATUS SEC_ENTRY RevertSecurityContext(PCtxtHandle context);
SECURITYAPI SECURITY_STATUS SEC_ENTRY MakeSignature(PCtxtHandle context, ULONG qop, PSecBufferDesc message,
                                                    ULONG sequence);
SECURITYAPI SECURITY_STATUS SEC_ENTRY VerifySignature(PCtxtHandle context, PSecBufferDesc message, ULONG sequence,
                                                      ULONG *qop);
SECURITYAPI SECURITY_STATUS SEC_ENTRY SealMessage(PCtxtHandle context, ULONG qop, PSecBufferDesc message,
                                                  ULONG sequence);
SECURITYAPI SECURITY_STATUS SEC_ENTRY UnsealMessage(PCtxtHandle context, PSecBufferDesc message, ULONG sequence,
                                                    ULONG *qop);
SECURITYAPI SECURITY_STATUS SEC_ENTRY ExportSecurityContext(PCtxtHandle context, ULONG flags, PSecBuffer packed,
                                                            void **token);
SECURITYAPI SECURITY_STATUS SEC_ENTRY ImportSecurityContextW(SEC_WCHAR *package, PSecBuffer packed, void *token,
                                                             PCtxtHandle context);
SECURITYAPI SECURITY_STATUS SEC_ENTRY ImportSecurityContextA(SEC_CHAR *package, PSecBuffer packed, void *token,
                                                             PCtxtHandle context);
SECURITYAPI SECURITY_STATUS SEC_ENTRY QuerySecurityContextToken(PCtxtHandle context, void **token);
SECURITYAPI SECURITY_STATUS SEC_ENTRY ChangeAccountPasswordW(SEC_WCHAR *package, SEC_WCHAR *domain, SEC_WCHAR *account,
                                                             SEC_WCHAR *old_password, SEC_WCHAR *new_password,
                                                             BOOLEAN impersonating, ULONG reserved,
                                                             PSecBufferDesc output);
SECURITYAPI SECURITY_STATUS SEC_ENTRY ChangeAccountPasswordA(SEC_CHAR *package, SEC_CHAR *domain, SEC_CHAR *account,
                                                             SEC_CHAR *old_password, SEC_CHAR *new_password,
                                                             BOOLEAN impersonating, ULONG reserved,
                                                             PSecBufferDesc output);
SECURITYAPI SECURITY_STATUS SEC_ENTRY AddSecurityPackageW(LPWSTR name, PSECURITY_PACKAGE_OPTIONS options);
SECURITYAPI SECURITY_STATUS SEC_ENTRY AddSecurityPackageA(LPSTR name, PSECURITY_PACKAGE_OPTIONS options);
SECURITYAPI SECURITY_STATUS SEC_ENTRY DeleteSecurityPackageW(LPWSTR name);
SECURITYAPI SECURITY_STATUS SEC_ENTRY DeleteSecurityPackageA(LPSTR name);
SECURITYAPI BOOLEAN SEC_ENTRY GetComputerObjectNameW(EXTENDED_NAME_FORMAT format, LPWSTR buffer, PULONG size);
SECURITYAPI BOOLEAN SEC_ENTRY GetComputerObjectNameA(EXTENDED_NAME_FORMAT format, LPSTR buffer, PULONG size);
SECURITYAPI BOOLEAN SEC_ENTRY TranslateNameW(LPCWSTR name, EXTENDED_NAME_FORMAT format, EXTENDED_NAME_FORMAT desired,
                                             LPWSTR buffer, PULONG size);
SECURITYAPI BOOLEAN SEC_ENTRY TranslateNameA(LPCSTR name, EXTENDED_NAME_FORMAT format, EXTENDED_NAME_FORMAT desired,
                                             LPSTR buffer, PULONG size);

#endif
