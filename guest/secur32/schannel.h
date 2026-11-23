/*
 * secur32's schannel package: a tls 1.2 and 1.3 client behind sspi. tls.c runs the protocol, cert.c uses crypt32 for
 * the server's certificates and schannel.c maps both onto the sspi calls.
 */
#ifndef MIME_SECUR32_SCHANNEL_H
#define MIME_SECUR32_SCHANNEL_H

#include "secur32_private.h"
#include <mime/win32/wincrypt.h>
#include <crypto/crypto.h>

/* ------------------------------------------------------------------ public schannel.h constants */

#define SP_PROT_SSL3_CLIENT 0x00000020
#define SP_PROT_TLS1_0_CLIENT 0x00000080
#define SP_PROT_TLS1_1_CLIENT 0x00000200
#define SP_PROT_TLS1_2_CLIENT 0x00000800
#define SP_PROT_TLS1_3_CLIENT 0x00002000
#define SP_PROT_TLS1_2_SERVER 0x00000400
#define SP_PROT_TLS1_3_SERVER 0x00001000

#define SCH_CRED_NO_SERVERNAME_CHECK 0x00000004
#define SCH_CRED_MANUAL_CRED_VALIDATION 0x00000008
#define SCH_CRED_NO_DEFAULT_CREDS 0x00000010
#define SCH_CRED_AUTO_CRED_VALIDATION 0x00000020
#define SCH_CRED_REVOCATION_CHECK_END_CERT 0x00000100
#define SCH_CRED_REVOCATION_CHECK_CHAIN 0x00000200
#define SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT 0x00000400
#define SCH_CRED_IGNORE_NO_REVOCATION_CHECK 0x00000800
#define SCH_CRED_IGNORE_REVOCATION_OFFLINE 0x00001000
#define SCH_CRED_DEFERRED_CRED_VALIDATION 0x04000000

#define ISC_REQ_REPLAY_DETECT 0x00000004
#define ISC_REQ_SEQUENCE_DETECT 0x00000008
#define ISC_REQ_CONFIDENTIALITY 0x00000010
#define ISC_REQ_USE_SUPPLIED_CREDS 0x00000080
#define ISC_REQ_ALLOCATE_MEMORY 0x00000100
#define ISC_REQ_EXTENDED_ERROR 0x00004000
#define ISC_REQ_STREAM 0x00008000
#define ISC_REQ_INTEGRITY 0x00010000
#define ISC_REQ_MANUAL_CRED_VALIDATION 0x00080000
#define ISC_RET_REPLAY_DETECT 0x00000004
#define ISC_RET_SEQUENCE_DETECT 0x00000008
#define ISC_RET_CONFIDENTIALITY 0x00000010
#define ISC_RET_USED_SUPPLIED_CREDS 0x00000080
#define ISC_RET_ALLOCATED_MEMORY 0x00000100
#define ISC_RET_EXTENDED_ERROR 0x00004000
#define ISC_RET_STREAM 0x00008000
#define ISC_RET_INTEGRITY 0x00010000
#define ISC_RET_MANUAL_CRED_VALIDATION 0x00080000

#define SECBUFFER_ALERT 17
#define SECBUFFER_APPLICATION_PROTOCOLS 18

#define SECPKG_ATTR_APPLICATION_PROTOCOL 35
#ifndef SECPKG_ATTR_REMOTE_CERT_CHAIN
#define SECPKG_ATTR_REMOTE_CERT_CHAIN 0x67
#endif
#ifndef SECPKG_ATTR_LOCAL_CERT_CONTEXT
#define SECPKG_ATTR_LOCAL_CERT_CONTEXT 0x54
#endif
#define SECPKG_ATTR_SESSION_INFO 0x5D
#define SECPKG_ATTR_CIPHER_INFO 0x64

#define SCHANNEL_RENEGOTIATE 0
#define SCHANNEL_SHUTDOWN 1
#define SCHANNEL_ALERT 2
#define SCHANNEL_SESSION 3

#define TLS1_ALERT_WARNING 1
#define TLS1_ALERT_FATAL 2

#ifndef SEC_I_NO_RENEGOTIATION
#define SEC_I_NO_RENEGOTIATION ((SECURITY_STATUS)0x00090360L)
#endif
#ifndef SEC_E_CERT_WRONG_USAGE
#define SEC_E_CERT_WRONG_USAGE ((SECURITY_STATUS)0x80090349L)
#endif
#ifndef SEC_E_APPLICATION_PROTOCOL_MISMATCH
#define SEC_E_APPLICATION_PROTOCOL_MISMATCH ((SECURITY_STATUS)0x80090367L)
#endif
#ifndef CRYPT_E_REVOKED
#define CRYPT_E_REVOKED ((SECURITY_STATUS)0x80092010L)
#endif
#ifndef CRYPT_E_NO_REVOCATION_CHECK
#define CRYPT_E_NO_REVOCATION_CHECK ((SECURITY_STATUS)0x80092012L)
#endif
#ifndef CRYPT_E_REVOCATION_OFFLINE
#define CRYPT_E_REVOCATION_OFFLINE ((SECURITY_STATUS)0x80092013L)
#endif

/* SCHANNEL_CRED, versions 3 and 4. */
typedef struct SCH_CRED_V4 {
    DWORD dwVersion;
    DWORD cCreds;
    PCCERT_CONTEXT *paCred;
    HCERTSTORE hRootStore;
    DWORD cMappers;
    void **aphMappers;
    DWORD cSupportedAlgs;
    ALG_ID *palgSupportedAlgs;
    DWORD grbitEnabledProtocols;
    DWORD dwMinimumCipherStrength;
    DWORD dwMaximumCipherStrength;
    DWORD dwSessionLifespan;
    DWORD dwFlags;
    DWORD dwCredFormat;
} SCH_CRED_V4;

typedef struct SCH_TLS_PARAMETERS {
    DWORD cAlpnIds;
    UNICODE_STRING *rgstrAlpnIds;
    DWORD grbitDisabledProtocols;
    DWORD cDisabledCrypto;
    void *pDisabledCrypto;
    DWORD dwFlags;
} SCH_TLS_PARAMETERS;

/* SCH_CREDENTIALS, version 5. */
typedef struct SCH_CRED_V5 {
    DWORD dwVersion;
    DWORD dwCredFormat;
    DWORD cCreds;
    PCCERT_CONTEXT *paCred;
    HCERTSTORE hRootStore;
    DWORD cMappers;
    void **aphMappers;
    DWORD dwSessionLifespan;
    DWORD dwFlags;
    DWORD cTlsParameters;
    SCH_TLS_PARAMETERS *pTlsParameters;
} SCH_CRED_V5;

typedef struct SCH_STREAM_SIZES {
    ULONG cbHeader;
    ULONG cbTrailer;
    ULONG cbMaximumMessage;
    ULONG cBuffers;
    ULONG cbBlockSize;
} SCH_STREAM_SIZES;

typedef struct SCH_CONNECTION_INFO {
    DWORD dwProtocol;
    ALG_ID aiCipher;
    DWORD dwCipherStrength;
    ALG_ID aiHash;
    DWORD dwHashStrength;
    ALG_ID aiExch;
    DWORD dwExchStrength;
} SCH_CONNECTION_INFO;

#define SCH_ALG_MAX 64

typedef struct SCH_CIPHER_INFO {
    DWORD dwVersion;
    DWORD dwProtocol;
    DWORD dwCipherSuite;
    DWORD dwBaseCipherSuite;
    WCHAR szCipherSuite[SCH_ALG_MAX];
    WCHAR szCipher[SCH_ALG_MAX];
    DWORD dwCipherLen;
    DWORD dwCipherBlockLen;
    WCHAR szHash[SCH_ALG_MAX];
    DWORD dwHashLen;
    WCHAR szExchange[SCH_ALG_MAX];
    DWORD dwMinExchangeLen;
    DWORD dwMaxExchangeLen;
    WCHAR szCertificate[SCH_ALG_MAX];
    DWORD dwKeyType;
} SCH_CIPHER_INFO;

typedef struct SCH_APPLICATION_PROTOCOL {
    ULONG ProtoNegoStatus;
    ULONG ProtoNegoExt;
    UCHAR ProtocolIdSize;
    UCHAR ProtocolId[255];
} SCH_APPLICATION_PROTOCOL;

typedef struct SCH_KEY_INFO_W {
    SEC_WCHAR *sSignatureAlgorithmName;
    SEC_WCHAR *sEncryptAlgorithmName;
    ULONG KeySize;
    ULONG SignatureAlgorithm;
    ULONG EncryptAlgorithm;
} SCH_KEY_INFO_W;

typedef struct SCH_SESSION_INFO {
    DWORD dwFlags;
    DWORD cbSessionId;
    BYTE rgbSessionId[32];
} SCH_SESSION_INFO;

/* ------------------------------------------------------------------ credentials */

typedef struct SCH_CONFIG {
    /* the client protocols that may be negotiated: SP_PROT_TLS1_2_CLIENT and SP_PROT_TLS1_3_CLIENT. */
    ULONG Protocols;
    ULONG Flags;
    ULONG MinStrength;
    ULONG MaxStrength;
    /* the server protocols that may be negotiated, SP_PROT_TLS1_2_SERVER, and the certificate a server answers with. */
    ULONG ServerProtocols;
    PCCERT_CONTEXT Certificate;
    /* the random thumbprint of the credential, which keys its cached sessions. */
    UINT64 Tag;
} SCH_CONFIG;

/* reads SCHANNEL_CRED or SCH_CREDENTIALS; NULL means the defaults. fails when no protocol is left. */
SECURITY_STATUS SchParseCredentials(const void *auth_data, SCH_CONFIG *config);
bool SecSchannelConfig(PCredHandle handle, SCH_CONFIG *config);

/* ------------------------------------------------------------------ certificates of a connection */

typedef struct TLS_CERTS {
    HCERTSTORE Store;
    PCCERT_CONTEXT Leaf;
} TLS_CERTS;

/* ------------------------------------------------------------------ client session cache, session.c */

typedef struct TLS_SESSION {
    struct TLS_SESSION *Next;
    UINT64 Tag;
    /* the target name of InitializeSecurityContext, empty for none. */
    PWSTR Target;
    USHORT Version;
    USHORT Suite;
    bool Ems;
    /* the tls 1.2 master secret, or the tls 1.3 resumption psk. */
    BYTE Secret[48];
    /* the connection that made it: its master secret, or its tls 1.3 resumption master secret. */
    BYTE Origin[48];
    BYTE SessionId[32];
    BYTE SessionIdSize;
    BYTE *Ticket;
    SIZE_T TicketSize;
    UINT32 AgeAdd;
    UINT32 Lifetime;
    ULONGLONG Created;
    /* the server's certificates, which a resumed connection reports. */
    TLS_CERTS Certs;
} TLS_SESSION;

/* a copy of the newest matching session; a tls 1.3 ticket leaves the cache as it is taken. */
TLS_SESSION *TlsSessionFind(UINT64 tag, PCWSTR target, ULONG protocols, bool (*usable)(const TLS_SESSION *, void *),
                            void *context);
TLS_SESSION *TlsSessionNew(UINT64 tag, PCWSTR target);
/* takes s over. */
void TlsSessionAdd(TLS_SESSION *s);
void TlsSessionFree(TLS_SESSION *s);
void TlsSessionForget(UINT64 tag, PCWSTR target, const BYTE *origin);
void TlsSessionPurge(UINT64 tag);

/* ------------------------------------------------------------------ tls engine, tls.c */

typedef struct TLS TLS;

typedef struct TLS_BUF {
    BYTE *Data;
    SIZE_T Size;
    SIZE_T Capacity;
    bool Failed;
} TLS_BUF;

bool TlsBufPut(TLS_BUF *b, const void *data, SIZE_T size);
void TlsBufFree(TLS_BUF *b);

TLS *TlsCreate(const SCH_CONFIG *config, ULONG requested, PCWSTR target);
/* a server end, which answers the first records of a client with its own. */
TLS *TlsCreateServer(const SCH_CONFIG *config, ULONG requested);
void TlsDestroy(TLS *t);
/* queues the ClientHello in the pending output. */
SECURITY_STATUS TlsStart(TLS *t, const BYTE *alpn, SIZE_T alpn_size);
/*
 * runs the handshake over the server's records. consumed is the bytes taken, missing what an incomplete record still
 * needs. records to send collect in the pending output.
 */
SECURITY_STATUS TlsHandshake(TLS *t, BYTE *input, SIZE_T size, SIZE_T *consumed, SIZE_T *missing);
/* records waiting to be sent; the caller hands them over and empties the buffer. */
TLS_BUF *TlsOutput(TLS *t);
bool TlsConnected(const TLS *t);
/* the InitializeSecurityContext call that follows SEC_I_RENEGOTIATE. */
SECURITY_STATUS TlsPostHandshake(TLS *t, SIZE_T size, SIZE_T *consumed);

typedef struct TLS_RECORD {
    /* points into the caller's buffer, decrypted in place. */
    BYTE *Header;
    SIZE_T HeaderSize;
    BYTE *Data;
    SIZE_T DataSize;
    SIZE_T TrailerSize;
    SIZE_T Total;
} TLS_RECORD;

SECURITY_STATUS TlsDecrypt(TLS *t, BYTE *input, SIZE_T size, TLS_RECORD *record, SIZE_T *missing);
/* encrypts size bytes at data, with the header before it and trailer room after it. */
SECURITY_STATUS TlsEncrypt(TLS *t, BYTE *header, BYTE *data, SIZE_T size, BYTE *trailer, SIZE_T trailer_room,
                           SIZE_T *trailer_used);
void TlsStreamSizes(const TLS *t, SCH_STREAM_SIZES *sizes);
void TlsConnectionInfo(const TLS *t, SCH_CONNECTION_INFO *info);
void TlsCipherInfo(const TLS *t, SCH_CIPHER_INFO *info);
void TlsApplicationProtocol(const TLS *t, SCH_APPLICATION_PROTOCOL *protocol);
void TlsSessionInfo(const TLS *t, SCH_SESSION_INFO *info);
SECURITY_STATUS TlsKeyInfo(const TLS *t, SCH_KEY_INFO_W *info, bool ansi);
/* the server's leaf certificate with the chain in its store, or NULL before it arrived. */
PCCERT_CONTEXT TlsRemoteCertificate(const TLS *t);
/* the certificate this handshake sent, NULL for none or a resumed session. */
PCCERT_CONTEXT TlsLocalCertificate(const TLS *t);
/* SSL_SESSION_DISABLE_RECONNECTS. */
void TlsDisableReconnects(TLS *t);
void TlsExpiry(const TLS *t, PTimeStamp expiry);
/* queues close_notify or another alert for the next InitializeSecurityContext. */
void TlsQueueAlert(TLS *t, BYTE level, BYTE number);
ULONG TlsReturnedFlags(const TLS *t);

/* ------------------------------------------------------------------ certificates, cert.c */

typedef struct TLS_PEER_KEY {
    /* 0 none, 1 rsa, 2 ecdsa. */
    int Type;
    MimeRsa *Rsa;
    MimeEcKey Ec;
} TLS_PEER_KEY;

/* a memory store of the server's certificates, leaf first; fails for one crypt32 cannot read. */
SECURITY_STATUS SchLoadCertificates(TLS_CERTS *certs, const BYTE *const *der, const SIZE_T *sizes, DWORD count);
SECURITY_STATUS SchLeafKey(const TLS_CERTS *certs, TLS_PEER_KEY *key);
void SchFreeKey(TLS_PEER_KEY *key);
/* crypt32 chain building and ssl policy, with the alert to send on failure. */
SECURITY_STATUS SchValidateChain(const TLS_CERTS *certs, PCWSTR server, ULONG flags, BYTE *alert);
void SchFreeCertificates(TLS_CERTS *certs);
void SchCopyCertificates(TLS_CERTS *to, const TLS_CERTS *from);
PCCERT_CONTEXT SchDuplicate(PCCERT_CONTEXT cert);
/* signs with the key the certificate's properties name. */
int SchCertificateKeyKind(PCCERT_CONTEXT cert, unsigned *bits);
bool SchSignWithCertificate(PCCERT_CONTEXT cert, unsigned scheme, const MimeHashAlg *hash, const BYTE *content,
                            SIZE_T size, BYTE *sig, SIZE_T *sig_size);
void SchFreeCertificate(PCCERT_CONTEXT cert);

/* crypto library hooks. */
bool SchRandom(void *out, SIZE_T size);

/* ------------------------------------------------------------------ sspi side, schannel.c */

SECURITY_STATUS SchInitialize(PCredHandle credential, PCtxtHandle context, PCWSTR target, ULONG flags,
                              PSecBufferDesc input, PCtxtHandle new_context, PSecBufferDesc output,
                              ULONG *attributes, PTimeStamp expiry);
SECURITY_STATUS SchAccept(PCredHandle credential, PCtxtHandle context, ULONG flags, PSecBufferDesc input,
                          PCtxtHandle new_context, PSecBufferDesc output, ULONG *attributes, PTimeStamp expiry);
SECURITY_STATUS SchEncrypt(TLS *t, PSecBufferDesc message);
SECURITY_STATUS SchDecrypt(TLS *t, PSecBufferDesc message);
SECURITY_STATUS SchQueryAttributes(TLS *t, ULONG attribute, void *buffer, bool ansi);
SECURITY_STATUS SchApplyControlToken(TLS *t, PSecBufferDesc input);

/* ------------------------------------------------------------------ context handles, context.c */

/* wraps t in a new schannel context handle. */
bool SecNewSchannelContext(const SEC_PACKAGE *package, TLS *t, PCtxtHandle handle);
/* the engine behind a live schannel context handle, locked; SecReleaseSchannelContext unlocks it. */
TLS *SecAcquireSchannelContext(PCtxtHandle handle);
void SecReleaseSchannelContext(PCtxtHandle handle);

#endif
