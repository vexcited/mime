#ifndef MIME_WIN32_BITS_H
#define MIME_WIN32_BITS_H

#include <mime/win32/objidl.h>
#include <mime/win32/oleauto.h>

MIME_DEFINE_GUID(CLSID_BackgroundCopyManager, 0x4991D34B, 0x80A1, 0x4291, 0x83, 0xB6, 0x33, 0x28, 0x36, 0x6B, 0x90, 0x97);
MIME_DEFINE_GUID(CLSID_BackgroundCopyQMgr, 0x69AD4AEE, 0x51BE, 0x439B, 0xA9, 0x2C, 0x86, 0xAE, 0x49, 0x0E, 0x8B, 0x30);
MIME_DEFINE_GUID(IID_IBackgroundCopyManager, 0x5CE34C0D, 0x0DC9, 0x4C1F, 0x89, 0x7C, 0xDA, 0xA1, 0xB7, 0x8C, 0xEE, 0x7C);
MIME_DEFINE_GUID(IID_IBackgroundCopyJob, 0x37668D37, 0x507E, 0x4160, 0x93, 0x16, 0x26, 0x30, 0x6D, 0x15, 0x0B, 0x12);
MIME_DEFINE_GUID(IID_IBackgroundCopyJob2, 0x54B50739, 0x686F, 0x45EB, 0x9D, 0xFF, 0xD6, 0xA9, 0xA0, 0xFA, 0xA9, 0xAF);
MIME_DEFINE_GUID(IID_IBackgroundCopyJob3, 0x443C8934, 0x90FF, 0x48ED, 0xBC, 0xDE, 0x26, 0xF5, 0xC7, 0x45, 0x00, 0x42);
MIME_DEFINE_GUID(IID_IBackgroundCopyJob4, 0x659CDEAE, 0x489E, 0x11D9, 0xA9, 0xCD, 0x00, 0x0D, 0x56, 0x96, 0x52, 0x51);
MIME_DEFINE_GUID(IID_IBackgroundCopyJob5, 0xE847030C, 0xBBBA, 0x4657, 0xAF, 0x6D, 0x48, 0x4A, 0xA4, 0x2B, 0xF1, 0xFE);
MIME_DEFINE_GUID(IID_IBackgroundCopyJobHttpOptions, 0xF1BD1079, 0x9F01, 0x4BDC, 0x80, 0x36, 0xF0, 0x9B, 0x70, 0x09, 0x50,
                 0x66);
MIME_DEFINE_GUID(IID_IBackgroundCopyJobHttpOptions2, 0xB591A192, 0xA405, 0x4FC3, 0x83, 0x23, 0x4C, 0x5C, 0x54, 0x25, 0x78,
                 0xFC);
MIME_DEFINE_GUID(IID_IBackgroundCopyJobHttpOptions3, 0x8A9263D3, 0xFD4C, 0x4EDA, 0x9B, 0x28, 0x30, 0x13, 0x2A, 0x4D, 0x4E,
                 0x3C);
MIME_DEFINE_GUID(IID_IBitsTokenOptions, 0x9A2584C3, 0xF7D2, 0x457A, 0x9A, 0x5E, 0x22, 0xB6, 0x7B, 0xFF, 0xC7, 0xD2);
MIME_DEFINE_GUID(IID_IBackgroundCopyFile, 0x01B7BD23, 0xFB88, 0x4A77, 0x84, 0x90, 0x58, 0x91, 0xD3, 0xE4, 0x65, 0x3A);
MIME_DEFINE_GUID(IID_IBackgroundCopyFile2, 0x83E81B93, 0x0873, 0x474D, 0x8A, 0x8C, 0xF2, 0x01, 0x8B, 0x1A, 0x93, 0x9C);
MIME_DEFINE_GUID(IID_IBackgroundCopyFile3, 0x659CDEAA, 0x489E, 0x11D9, 0xA9, 0xCD, 0x00, 0x0D, 0x56, 0x96, 0x52, 0x51);
MIME_DEFINE_GUID(IID_IBackgroundCopyFile4, 0xEF7E0655, 0x7888, 0x4960, 0xB0, 0xE5, 0x73, 0x08, 0x46, 0xE0, 0x34, 0x92);
MIME_DEFINE_GUID(IID_IBackgroundCopyFile5, 0x85C1657F, 0xDAFC, 0x40E8, 0x88, 0x34, 0xDF, 0x18, 0xEA, 0x25, 0x71, 0x7E);
MIME_DEFINE_GUID(IID_IBackgroundCopyFile6, 0xCF6784F7, 0xD677, 0x49FD, 0x93, 0x68, 0xCB, 0x47, 0xAE, 0xE9, 0xD1, 0xAD);
MIME_DEFINE_GUID(IID_IBackgroundCopyError, 0x19C613A0, 0xFCB8, 0x4F28, 0x81, 0xAE, 0x89, 0x7C, 0x3D, 0x07, 0x8F, 0x81);
MIME_DEFINE_GUID(IID_IEnumBackgroundCopyJobs, 0x1AF4F612, 0x3B71, 0x466F, 0x8F, 0x58, 0x7B, 0x6F, 0x73, 0xAC, 0x57, 0xAD);
MIME_DEFINE_GUID(IID_IEnumBackgroundCopyFiles, 0xCA51E165, 0xC365, 0x424C, 0x8D, 0x41, 0x24, 0xAA, 0xA4, 0xFF, 0x3C, 0x40);
MIME_DEFINE_GUID(IID_IBackgroundCopyCallback, 0x97EA99C7, 0x0186, 0x4AD4, 0x8D, 0xF9, 0xC5, 0xB4, 0xE0, 0xED, 0x6B, 0x22);
MIME_DEFINE_GUID(IID_IBackgroundCopyCallback2, 0x659CDEAC, 0x489E, 0x11D9, 0xA9, 0xCD, 0x00, 0x0D, 0x56, 0x96, 0x52, 0x51);
MIME_DEFINE_GUID(IID_IBackgroundCopyCallback3, 0x98C97BD2, 0xE32B, 0x4AD8, 0xA5, 0x28, 0x95, 0xFD, 0x8B, 0x16, 0xBD, 0x42);
MIME_DEFINE_GUID(IID_IBackgroundCopyServerCertificateValidationCallback, 0x4CEC0D02, 0xDEF7, 0x4158, 0x81, 0x3A, 0xC3, 0x2A,
                 0x46, 0x94, 0x5F, 0xF7);

#define BG_NOTIFY_JOB_TRANSFERRED 0x0001
#define BG_NOTIFY_JOB_ERROR 0x0002
#define BG_NOTIFY_DISABLE 0x0004
#define BG_NOTIFY_JOB_MODIFICATION 0x0008
#define BG_NOTIFY_FILE_TRANSFERRED 0x0010
#define BG_NOTIFY_FILE_RANGES_TRANSFERRED 0x0020
#define BG_JOB_ENUM_ALL_USERS 0x0001
#define BG_LENGTH_TO_EOF ((UINT64)-1)
#define BG_SIZE_UNKNOWN ((UINT64)-1)

#define BG_SSL_ENABLE_CRL_CHECK 0x0001
#define BG_SSL_IGNORE_CERT_CN_INVALID 0x0002
#define BG_SSL_IGNORE_CERT_DATE_INVALID 0x0004
#define BG_SSL_IGNORE_UNKNOWN_CA 0x0008
#define BG_SSL_IGNORE_CERT_WRONG_USAGE 0x0010
#define BG_HTTP_REDIRECT_POLICY_MASK 0x0700
#define BG_HTTP_REDIRECT_POLICY_ALLOW_SILENT 0x0000
#define BG_HTTP_REDIRECT_POLICY_ALLOW_REPORT 0x0100
#define BG_HTTP_REDIRECT_POLICY_DISALLOW 0x0200
#define BG_HTTP_REDIRECT_POLICY_ALLOW_HTTPS_TO_HTTP 0x0800

#define BG_E_NOT_FOUND ((HRESULT)0x80200001L)
#define BG_E_INVALID_STATE ((HRESULT)0x80200002L)
#define BG_E_EMPTY ((HRESULT)0x80200003L)
#define BG_E_FILE_NOT_AVAILABLE ((HRESULT)0x80200004L)
#define BG_E_PROTOCOL_NOT_AVAILABLE ((HRESULT)0x80200005L)
#define BG_S_ERROR_CONTEXT_NONE ((HRESULT)0x00200006L)
#define BG_E_ERROR_CONTEXT_UNKNOWN ((HRESULT)0x80200007L)
#define BG_E_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER ((HRESULT)0x80200008L)
#define BG_E_ERROR_CONTEXT_LOCAL_FILE ((HRESULT)0x80200009L)
#define BG_E_ERROR_CONTEXT_REMOTE_FILE ((HRESULT)0x8020000AL)
#define BG_E_ERROR_CONTEXT_GENERAL_TRANSPORT ((HRESULT)0x8020000BL)
#define BG_E_ERROR_CONTEXT_QUEUE_MANAGER_NOTIFICATION ((HRESULT)0x8020000CL)
#define BG_E_DESTINATION_LOCKED ((HRESULT)0x8020000DL)
#define BG_E_VOLUME_CHANGED ((HRESULT)0x8020000EL)
#define BG_E_ERROR_INFORMATION_UNAVAILABLE ((HRESULT)0x8020000FL)
#define BG_E_NETWORK_DISCONNECTED ((HRESULT)0x80200010L)
#define BG_E_MISSING_FILE_SIZE ((HRESULT)0x80200011L)
#define BG_E_INSUFFICIENT_HTTP_SUPPORT ((HRESULT)0x80200012L)
#define BG_E_INSUFFICIENT_RANGE_SUPPORT ((HRESULT)0x80200013L)
#define BG_E_REMOTE_NOT_SUPPORTED ((HRESULT)0x80200014L)
#define BG_E_NEW_OWNER_DIFF_MAPPING ((HRESULT)0x80200015L)
#define BG_E_NEW_OWNER_NO_FILE_ACCESS ((HRESULT)0x80200016L)
#define BG_S_PARTIAL_COMPLETE ((HRESULT)0x00200017L)
#define BG_E_PROXY_LIST_TOO_LARGE ((HRESULT)0x80200018L)
#define BG_E_PROXY_BYPASS_LIST_TOO_LARGE ((HRESULT)0x80200019L)
#define BG_S_UNABLE_TO_DELETE_FILES ((HRESULT)0x0020001AL)
#define BG_E_INVALID_SERVER_RESPONSE ((HRESULT)0x8020001BL)
#define BG_E_TOO_MANY_FILES ((HRESULT)0x8020001CL)
#define BG_E_LOCAL_FILE_CHANGED ((HRESULT)0x8020001DL)
#define BG_E_ERROR_CONTEXT_REMOTE_APPLICATION ((HRESULT)0x8020001EL)
#define BG_E_SESSION_NOT_FOUND ((HRESULT)0x8020001FL)
#define BG_E_TOO_LARGE ((HRESULT)0x80200020L)
#define BG_E_STRING_TOO_LONG ((HRESULT)0x80200021L)
#define BG_E_CLIENT_SERVER_PROTOCOL_MISMATCH ((HRESULT)0x80200022L)
#define BG_E_SERVER_EXECUTE_ENABLE ((HRESULT)0x80200023L)
#define BG_E_NO_PROGRESS ((HRESULT)0x80200024L)
#define BG_E_USERNAME_TOO_LARGE ((HRESULT)0x80200025L)
#define BG_E_PASSWORD_TOO_LARGE ((HRESULT)0x80200026L)
#define BG_E_INVALID_AUTH_TARGET ((HRESULT)0x80200027L)
#define BG_E_INVALID_AUTH_SCHEME ((HRESULT)0x80200028L)
#define BG_E_FILE_NOT_FOUND ((HRESULT)0x80200029L)
#define BG_S_PROXY_CHANGED ((HRESULT)0x0020002AL)
#define BG_E_INVALID_RANGE ((HRESULT)0x8020002BL)
#define BG_E_OVERLAPPING_RANGES ((HRESULT)0x8020002CL)
#define BG_E_CONNECT_FAILURE ((HRESULT)0x8020002DL)
#define BG_E_CONNECTION_CLOSED ((HRESULT)0x8020002EL)
#define BG_E_BLOCKED_BY_POLICY ((HRESULT)0x8020003EL)
#define BG_E_INVALID_PROXY_INFO ((HRESULT)0x8020003FL)
#define BG_E_INVALID_CREDENTIALS ((HRESULT)0x80200040L)
#define BG_E_RECORD_DELETED ((HRESULT)0x80200042L)
#define BG_E_PEERCACHING_DISABLED ((HRESULT)0x80200047L)
#define BG_E_BUSYCACHERECORD ((HRESULT)0x80200048L)
#define BG_E_TOO_MANY_JOBS_PER_USER ((HRESULT)0x80200049L)
#define BG_E_TOO_MANY_JOBS_PER_MACHINE ((HRESULT)0x80200050L)
#define BG_E_TOO_MANY_FILES_IN_JOB ((HRESULT)0x80200051L)
#define BG_E_TOO_MANY_RANGES_IN_FILE ((HRESULT)0x80200052L)
#define BG_E_VALIDATION_FAILED ((HRESULT)0x80200053L)
#define BG_E_MAXDOWNLOAD_TIMEOUT ((HRESULT)0x80200054L)
#define BG_S_OVERRIDDEN_BY_POLICY ((HRESULT)0x00200055L)
#define BG_E_TOKEN_REQUIRED ((HRESULT)0x80200056L)
#define BG_E_UNKNOWN_PROPERTY_ID ((HRESULT)0x80200057L)
#define BG_E_READ_ONLY_PROPERTY ((HRESULT)0x80200058L)
#define BG_E_BLOCKED_BY_COST_TRANSFER_POLICY ((HRESULT)0x80200059L)
#define BG_E_PROPERTY_SUPPORTED_FOR_DOWNLOAD_JOBS_ONLY ((HRESULT)0x80200060L)
#define BG_E_READ_ONLY_PROPERTY_AFTER_ADDFILE ((HRESULT)0x80200061L)
#define BG_E_READ_ONLY_PROPERTY_AFTER_RESUME ((HRESULT)0x80200062L)
#define BG_E_MAX_DOWNLOAD_SIZE_INVALID_VALUE ((HRESULT)0x80200063L)
#define BG_E_MAX_DOWNLOAD_SIZE_LIMIT_REACHED ((HRESULT)0x80200064L)
#define BG_E_STANDBY_MODE ((HRESULT)0x80200065L)
#define BG_E_USE_STORED_CREDENTIALS_NOT_SUPPORTED ((HRESULT)0x80200066L)
#define BG_E_BLOCKED_BY_BATTERY_POLICY ((HRESULT)0x80200067L)
#define BG_E_BLOCKED_BY_BATTERY_SAVER ((HRESULT)0x80200068L)
#define BG_E_WATCHDOG_TIMEOUT ((HRESULT)0x80200069L)
#define BG_E_APP_PACKAGE_NOT_FOUND ((HRESULT)0x8020006AL)
#define BG_E_APP_PACKAGE_SCENARIO_NOT_SUPPORTED ((HRESULT)0x8020006BL)
#define BG_E_DATABASE_CORRUPT ((HRESULT)0x8020006CL)
#define BG_E_RANDOM_ACCESS_NOT_SUPPORTED ((HRESULT)0x8020006DL)
#define BG_E_BLOCKED_BY_BACKGROUND_ACCESS_POLICY ((HRESULT)0x8020006EL)
#define BG_E_BLOCKED_BY_GAME_MODE ((HRESULT)0x8020006FL)
#define BG_E_BLOCKED_BY_SYSTEM_POLICY ((HRESULT)0x80200070L)
#define BG_E_NOT_SUPPORTED_WITH_CUSTOM_HTTP_METHOD ((HRESULT)0x80200071L)
#define BG_E_UNSUPPORTED_JOB_CONFIGURATION ((HRESULT)0x80200072L)
#define BG_E_REMOTE_FILE_CHANGED ((HRESULT)0x80200073L)
#define BG_E_SERVER_CERT_VALIDATION_INTERFACE_REQUIRED ((HRESULT)0x80200074L)
#define BG_E_READ_ONLY_WHEN_JOB_ACTIVE ((HRESULT)0x80200075L)
#define BG_E_ERROR_CONTEXT_SERVER_CERTIFICATE_CALLBACK ((HRESULT)0x80200076L)
#define BG_E_HTTP_ERROR_404 ((HRESULT)0x80190194L)

typedef enum { BG_JOB_PRIORITY_FOREGROUND, BG_JOB_PRIORITY_HIGH, BG_JOB_PRIORITY_NORMAL, BG_JOB_PRIORITY_LOW } BG_JOB_PRIORITY;
typedef enum {
    BG_JOB_STATE_QUEUED,
    BG_JOB_STATE_CONNECTING,
    BG_JOB_STATE_TRANSFERRING,
    BG_JOB_STATE_SUSPENDED,
    BG_JOB_STATE_ERROR,
    BG_JOB_STATE_TRANSIENT_ERROR,
    BG_JOB_STATE_TRANSFERRED,
    BG_JOB_STATE_ACKNOWLEDGED,
    BG_JOB_STATE_CANCELLED,
} BG_JOB_STATE;
typedef enum { BG_JOB_TYPE_DOWNLOAD, BG_JOB_TYPE_UPLOAD, BG_JOB_TYPE_UPLOAD_REPLY } BG_JOB_TYPE;
typedef enum {
    BG_JOB_PROXY_USAGE_PRECONFIG,
    BG_JOB_PROXY_USAGE_NO_PROXY,
    BG_JOB_PROXY_USAGE_OVERRIDE,
    BG_JOB_PROXY_USAGE_AUTODETECT,
} BG_JOB_PROXY_USAGE;
typedef enum {
    BG_ERROR_CONTEXT_NONE,
    BG_ERROR_CONTEXT_UNKNOWN,
    BG_ERROR_CONTEXT_GENERAL_QUEUE_MANAGER,
    BG_ERROR_CONTEXT_QUEUE_MANAGER_NOTIFICATION,
    BG_ERROR_CONTEXT_LOCAL_FILE,
    BG_ERROR_CONTEXT_REMOTE_FILE,
    BG_ERROR_CONTEXT_GENERAL_TRANSPORT,
    BG_ERROR_CONTEXT_REMOTE_APPLICATION,
    BG_ERROR_CONTEXT_SERVER_CERTIFICATE_CALLBACK,
} BG_ERROR_CONTEXT;
typedef enum { BG_AUTH_TARGET_SERVER = 1, BG_AUTH_TARGET_PROXY } BG_AUTH_TARGET;
typedef enum {
    BG_AUTH_SCHEME_BASIC = 1,
    BG_AUTH_SCHEME_DIGEST,
    BG_AUTH_SCHEME_NTLM,
    BG_AUTH_SCHEME_NEGOTIATE,
    BG_AUTH_SCHEME_PASSPORT,
} BG_AUTH_SCHEME;
typedef enum {
    BG_CERT_STORE_LOCATION_CURRENT_USER,
    BG_CERT_STORE_LOCATION_LOCAL_MACHINE,
    BG_CERT_STORE_LOCATION_CURRENT_SERVICE,
    BG_CERT_STORE_LOCATION_SERVICES,
    BG_CERT_STORE_LOCATION_USERS,
    BG_CERT_STORE_LOCATION_CURRENT_USER_GROUP_POLICY,
    BG_CERT_STORE_LOCATION_LOCAL_MACHINE_GROUP_POLICY,
    BG_CERT_STORE_LOCATION_LOCAL_MACHINE_ENTERPRISE,
} BG_CERT_STORE_LOCATION;
typedef enum {
    BITS_JOB_PROPERTY_ID_COST_FLAGS = 1,
    BITS_JOB_PROPERTY_NOTIFICATION_CLSID = 2,
    BITS_JOB_PROPERTY_DYNAMIC_CONTENT = 3,
    BITS_JOB_PROPERTY_HIGH_PERFORMANCE = 4,
    BITS_JOB_PROPERTY_MAX_DOWNLOAD_SIZE = 5,
    BITS_JOB_PROPERTY_USE_STORED_CREDENTIALS = 7,
    BITS_JOB_PROPERTY_MINIMUM_NOTIFICATION_INTERVAL_MS = 9,
    BITS_JOB_PROPERTY_ON_DEMAND_MODE = 10,
} BITS_JOB_PROPERTY_ID;
typedef enum { BITS_FILE_PROPERTY_ID_HTTP_RESPONSE_HEADERS = 1 } BITS_FILE_PROPERTY_ID;

typedef struct _BG_FILE_INFO {
    LPWSTR RemoteName;
    LPWSTR LocalName;
} BG_FILE_INFO;
typedef struct _BG_FILE_PROGRESS {
    UINT64 BytesTotal;
    UINT64 BytesTransferred;
    BOOL Completed;
} BG_FILE_PROGRESS;
typedef struct _BG_JOB_PROGRESS {
    UINT64 BytesTotal;
    UINT64 BytesTransferred;
    ULONG FilesTotal;
    ULONG FilesTransferred;
} BG_JOB_PROGRESS;
typedef struct _BG_JOB_TIMES {
    FILETIME CreationTime;
    FILETIME ModificationTime;
    FILETIME TransferCompletionTime;
} BG_JOB_TIMES;
typedef struct _BG_JOB_REPLY_PROGRESS {
    UINT64 BytesTotal;
    UINT64 BytesTransferred;
} BG_JOB_REPLY_PROGRESS;
#ifndef MIME_BG_FILE_RANGE_DEFINED
#define MIME_BG_FILE_RANGE_DEFINED
typedef struct _BG_FILE_RANGE {
    UINT64 InitialOffset;
    UINT64 Length;
} BG_FILE_RANGE;
#endif
typedef struct {
    LPWSTR UserName;
    LPWSTR Password;
} BG_BASIC_CREDENTIALS;
typedef struct {
    BG_AUTH_TARGET Target;
    BG_AUTH_SCHEME Scheme;
    union {
        BG_BASIC_CREDENTIALS Basic;
    } Credentials;
} BG_AUTH_CREDENTIALS;
typedef union {
    DWORD Dword;
    GUID ClsID;
    BOOL Enable;
    UINT64 Uint64;
    struct {
        BG_AUTH_TARGET Target;
    } Target;
} BITS_JOB_PROPERTY_VALUE;
typedef union {
    LPWSTR String;
} BITS_FILE_PROPERTY_VALUE;

typedef struct IBackgroundCopyFile IBackgroundCopyFile;
typedef struct IBackgroundCopyJob IBackgroundCopyJob;
typedef struct IBackgroundCopyError IBackgroundCopyError;
typedef struct IEnumBackgroundCopyFiles IEnumBackgroundCopyFiles;
typedef struct IEnumBackgroundCopyJobs IEnumBackgroundCopyJobs;

#define BITS_IUNKNOWN(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(T * This, REFIID riid, void **object);                                  \
    ULONG(STDMETHODCALLTYPE *AddRef)(T * This);                                                                        \
    ULONG(STDMETHODCALLTYPE *Release)(T * This);

#define BITS_INTERFACE(name, methods)                                                                                  \
    typedef struct name name;                                                                                          \
    typedef struct name##Vtbl {                                                                                        \
        BITS_IUNKNOWN(name)                                                                                            \
        methods(name)                                                                                                  \
    } name##Vtbl;                                                                                                      \
    struct name {                                                                                                      \
        const name##Vtbl *lpVtbl;                                                                                      \
    };

#define BITS_FILE_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetRemoteName)(T * This, LPWSTR * name);                                                \
    HRESULT(STDMETHODCALLTYPE *GetLocalName)(T * This, LPWSTR * name);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetProgress)(T * This, BG_FILE_PROGRESS * progress);
#define BITS_FILE2_METHODS(T)                                                                                          \
    BITS_FILE_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *GetFileRanges)(T * This, DWORD * count, BG_FILE_RANGE * *ranges);                       \
    HRESULT(STDMETHODCALLTYPE *SetRemoteName)(T * This, LPCWSTR name);
#define BITS_FILE3_METHODS(T)                                                                                          \
    BITS_FILE2_METHODS(T)                                                                                              \
    HRESULT(STDMETHODCALLTYPE *GetTemporaryName)(T * This, LPWSTR * name);                                             \
    HRESULT(STDMETHODCALLTYPE *SetValidationState)(T * This, BOOL state);                                              \
    HRESULT(STDMETHODCALLTYPE *GetValidationState)(T * This, BOOL * state);                                            \
    HRESULT(STDMETHODCALLTYPE *IsDownloadedFromPeer)(T * This, BOOL * peer);
#define BITS_FILE4_METHODS(T)                                                                                          \
    BITS_FILE3_METHODS(T)                                                                                              \
    HRESULT(STDMETHODCALLTYPE *GetPeerDownloadStats)(T * This, UINT64 *origin, UINT64 *peers);
#define BITS_FILE5_METHODS(T)                                                                                          \
    BITS_FILE4_METHODS(T)                                                                                              \
    HRESULT(STDMETHODCALLTYPE *SetProperty)(T * This, BITS_FILE_PROPERTY_ID id, BITS_FILE_PROPERTY_VALUE value);       \
    HRESULT(STDMETHODCALLTYPE *GetProperty)(T * This, BITS_FILE_PROPERTY_ID id, BITS_FILE_PROPERTY_VALUE * value);
#define BITS_FILE6_METHODS(T)                                                                                          \
    BITS_FILE5_METHODS(T)                                                                                              \
    HRESULT(STDMETHODCALLTYPE *UpdateDownloadPosition)(T * This, UINT64 offset);                                       \
    HRESULT(STDMETHODCALLTYPE *RequestFileRanges)(T * This, DWORD count, const BG_FILE_RANGE *ranges);                 \
    HRESULT(STDMETHODCALLTYPE *GetFilledFileRanges)(T * This, DWORD * count, BG_FILE_RANGE * *ranges);

BITS_INTERFACE(IBackgroundCopyFile6, BITS_FILE6_METHODS)
struct IBackgroundCopyFile {
    const IBackgroundCopyFile6Vtbl *lpVtbl;
};
typedef IBackgroundCopyFile6 IBackgroundCopyFile2, IBackgroundCopyFile3, IBackgroundCopyFile4, IBackgroundCopyFile5;

#define BITS_ENUM_METHODS(T, item)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, ULONG count, item * *items, ULONG * fetched);                           \
    HRESULT(STDMETHODCALLTYPE *Skip)(T * This, ULONG count);                                                           \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, T * *copy);                                                            \
    HRESULT(STDMETHODCALLTYPE *GetCount)(T * This, ULONG * count);
#define BITS_ENUM_FILES_METHODS(T) BITS_ENUM_METHODS(T, IBackgroundCopyFile)
#define BITS_ENUM_JOBS_METHODS(T) BITS_ENUM_METHODS(T, IBackgroundCopyJob)
typedef struct IEnumBackgroundCopyFilesVtbl {
    BITS_IUNKNOWN(IEnumBackgroundCopyFiles)
    BITS_ENUM_FILES_METHODS(IEnumBackgroundCopyFiles)
} IEnumBackgroundCopyFilesVtbl;
struct IEnumBackgroundCopyFiles {
    const IEnumBackgroundCopyFilesVtbl *lpVtbl;
};
typedef struct IEnumBackgroundCopyJobsVtbl {
    BITS_IUNKNOWN(IEnumBackgroundCopyJobs)
    BITS_ENUM_JOBS_METHODS(IEnumBackgroundCopyJobs)
} IEnumBackgroundCopyJobsVtbl;
struct IEnumBackgroundCopyJobs {
    const IEnumBackgroundCopyJobsVtbl *lpVtbl;
};

#define BITS_ERROR_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *GetError)(T * This, BG_ERROR_CONTEXT * context, HRESULT * code);                        \
    HRESULT(STDMETHODCALLTYPE *GetFile)(T * This, IBackgroundCopyFile * *file);                                        \
    HRESULT(STDMETHODCALLTYPE *GetErrorDescription)(T * This, DWORD language, LPWSTR * text);                          \
    HRESULT(STDMETHODCALLTYPE *GetErrorContextDescription)(T * This, DWORD language, LPWSTR * text);                   \
    HRESULT(STDMETHODCALLTYPE *GetProtocol)(T * This, LPWSTR * protocol);
typedef struct IBackgroundCopyErrorVtbl {
    BITS_IUNKNOWN(IBackgroundCopyError)
    BITS_ERROR_METHODS(IBackgroundCopyError)
} IBackgroundCopyErrorVtbl;
struct IBackgroundCopyError {
    const IBackgroundCopyErrorVtbl *lpVtbl;
};

#define BITS_JOB_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *AddFileSet)(T * This, ULONG count, BG_FILE_INFO * files);                               \
    HRESULT(STDMETHODCALLTYPE *AddFile)(T * This, LPCWSTR remote, LPCWSTR local);                                      \
    HRESULT(STDMETHODCALLTYPE *EnumFiles)(T * This, IEnumBackgroundCopyFiles * *files);                                \
    HRESULT(STDMETHODCALLTYPE *Suspend)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Resume)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *Cancel)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *Complete)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetId)(T * This, GUID * id);                                                            \
    HRESULT(STDMETHODCALLTYPE *GetType)(T * This, BG_JOB_TYPE * type);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetProgress)(T * This, BG_JOB_PROGRESS * progress);                                     \
    HRESULT(STDMETHODCALLTYPE *GetTimes)(T * This, BG_JOB_TIMES * times);                                              \
    HRESULT(STDMETHODCALLTYPE *GetState)(T * This, BG_JOB_STATE * state);                                              \
    HRESULT(STDMETHODCALLTYPE *GetError)(T * This, IBackgroundCopyError * *error);                                     \
    HRESULT(STDMETHODCALLTYPE *GetOwner)(T * This, LPWSTR * owner);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetDisplayName)(T * This, LPCWSTR name);                                                \
    HRESULT(STDMETHODCALLTYPE *GetDisplayName)(T * This, LPWSTR * name);                                               \
    HRESULT(STDMETHODCALLTYPE *SetDescription)(T * This, LPCWSTR text);                                                \
    HRESULT(STDMETHODCALLTYPE *GetDescription)(T * This, LPWSTR * text);                                               \
    HRESULT(STDMETHODCALLTYPE *SetPriority)(T * This, BG_JOB_PRIORITY priority);                                       \
    HRESULT(STDMETHODCALLTYPE *GetPriority)(T * This, BG_JOB_PRIORITY * priority);                                     \
    HRESULT(STDMETHODCALLTYPE *SetNotifyFlags)(T * This, ULONG flags);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetNotifyFlags)(T * This, ULONG * flags);                                               \
    HRESULT(STDMETHODCALLTYPE *SetNotifyInterface)(T * This, IUnknown * notify);                                       \
    HRESULT(STDMETHODCALLTYPE *GetNotifyInterface)(T * This, IUnknown * *notify);                                      \
    HRESULT(STDMETHODCALLTYPE *SetMinimumRetryDelay)(T * This, ULONG seconds);                                         \
    HRESULT(STDMETHODCALLTYPE *GetMinimumRetryDelay)(T * This, ULONG * seconds);                                       \
    HRESULT(STDMETHODCALLTYPE *SetNoProgressTimeout)(T * This, ULONG seconds);                                         \
    HRESULT(STDMETHODCALLTYPE *GetNoProgressTimeout)(T * This, ULONG * seconds);                                       \
    HRESULT(STDMETHODCALLTYPE *GetErrorCount)(T * This, ULONG * errors);                                               \
    HRESULT(STDMETHODCALLTYPE *SetProxySettings)(T * This, BG_JOB_PROXY_USAGE usage, LPCWSTR list, LPCWSTR bypass);    \
    HRESULT(STDMETHODCALLTYPE *GetProxySettings)(T * This, BG_JOB_PROXY_USAGE * usage, LPWSTR * list,                  \
                                                 LPWSTR * bypass);                                                     \
    HRESULT(STDMETHODCALLTYPE *TakeOwnership)(T * This);
#define BITS_JOB2_METHODS(T)                                                                                           \
    BITS_JOB_METHODS(T)                                                                                                \
    HRESULT(STDMETHODCALLTYPE *SetNotifyCmdLine)(T * This, LPCWSTR program, LPCWSTR parameters);                       \
    HRESULT(STDMETHODCALLTYPE *GetNotifyCmdLine)(T * This, LPWSTR * program, LPWSTR * parameters);                     \
    HRESULT(STDMETHODCALLTYPE *GetReplyProgress)(T * This, BG_JOB_REPLY_PROGRESS * progress);                          \
    HRESULT(STDMETHODCALLTYPE *GetReplyData)(T * This, BYTE * *buffer, UINT64 * length);                               \
    HRESULT(STDMETHODCALLTYPE *SetReplyFileName)(T * This, LPCWSTR name);                                              \
    HRESULT(STDMETHODCALLTYPE *GetReplyFileName)(T * This, LPWSTR * name);                                             \
    HRESULT(STDMETHODCALLTYPE *SetCredentials)(T * This, BG_AUTH_CREDENTIALS * credentials);                           \
    HRESULT(STDMETHODCALLTYPE *RemoveCredentials)(T * This, BG_AUTH_TARGET target, BG_AUTH_SCHEME scheme);
#define BITS_JOB3_METHODS(T)                                                                                           \
    BITS_JOB2_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *ReplaceRemotePrefix)(T * This, LPCWSTR old_prefix, LPCWSTR new_prefix);                 \
    HRESULT(STDMETHODCALLTYPE *AddFileWithRanges)(T * This, LPCWSTR remote, LPCWSTR local, DWORD count,                \
                                                  BG_FILE_RANGE * ranges);                                             \
    HRESULT(STDMETHODCALLTYPE *SetFileACLFlags)(T * This, DWORD flags);                                                \
    HRESULT(STDMETHODCALLTYPE *GetFileACLFlags)(T * This, DWORD * flags);
#define BITS_JOB4_METHODS(T)                                                                                           \
    BITS_JOB3_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *SetPeerCachingFlags)(T * This, DWORD flags);                                            \
    HRESULT(STDMETHODCALLTYPE *GetPeerCachingFlags)(T * This, DWORD * flags);                                          \
    HRESULT(STDMETHODCALLTYPE *GetOwnerIntegrityLevel)(T * This, ULONG * level);                                       \
    HRESULT(STDMETHODCALLTYPE *GetOwnerElevationState)(T * This, BOOL * elevated);                                     \
    HRESULT(STDMETHODCALLTYPE *SetMaximumDownloadTime)(T * This, ULONG seconds);                                       \
    HRESULT(STDMETHODCALLTYPE *GetMaximumDownloadTime)(T * This, ULONG * seconds);
#define BITS_JOB5_METHODS(T)                                                                                           \
    BITS_JOB4_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *SetProperty)(T * This, BITS_JOB_PROPERTY_ID id, BITS_JOB_PROPERTY_VALUE value);         \
    HRESULT(STDMETHODCALLTYPE *GetProperty)(T * This, BITS_JOB_PROPERTY_ID id, BITS_JOB_PROPERTY_VALUE * value);

BITS_INTERFACE(IBackgroundCopyJob5, BITS_JOB5_METHODS)
struct IBackgroundCopyJob {
    const IBackgroundCopyJob5Vtbl *lpVtbl;
};
typedef IBackgroundCopyJob5 IBackgroundCopyJob2, IBackgroundCopyJob3, IBackgroundCopyJob4;

#define BITS_HTTP_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetClientCertificateByID)(T * This, BG_CERT_STORE_LOCATION location, LPCWSTR store,     \
                                                         BYTE * hash);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetClientCertificateByName)(T * This, BG_CERT_STORE_LOCATION location, LPCWSTR store,   \
                                                           LPCWSTR subject);                                           \
    HRESULT(STDMETHODCALLTYPE *RemoveClientCertificate)(T * This);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetClientCertificate)(T * This, BG_CERT_STORE_LOCATION * location, LPWSTR * store,      \
                                                     BYTE * *hash, LPWSTR * subject);                                  \
    HRESULT(STDMETHODCALLTYPE *SetCustomHeaders)(T * This, LPCWSTR headers);                                           \
    HRESULT(STDMETHODCALLTYPE *GetCustomHeaders)(T * This, LPWSTR * headers);                                          \
    HRESULT(STDMETHODCALLTYPE *SetSecurityFlags)(T * This, ULONG flags);                                               \
    HRESULT(STDMETHODCALLTYPE *GetSecurityFlags)(T * This, ULONG * flags);
#define BITS_HTTP2_METHODS(T)                                                                                          \
    BITS_HTTP_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *SetHttpMethod)(T * This, LPCWSTR method);                                               \
    HRESULT(STDMETHODCALLTYPE *GetHttpMethod)(T * This, LPWSTR * method);
#define BITS_HTTP3_METHODS(T)                                                                                          \
    BITS_HTTP2_METHODS(T)                                                                                              \
    HRESULT(STDMETHODCALLTYPE *SetServerCertificateValidationInterface)(T * This, IUnknown * callback);                \
    HRESULT(STDMETHODCALLTYPE *MakeCustomHeadersWriteOnly)(T * This);
BITS_INTERFACE(IBackgroundCopyJobHttpOptions3, BITS_HTTP3_METHODS)
typedef IBackgroundCopyJobHttpOptions3 IBackgroundCopyJobHttpOptions, IBackgroundCopyJobHttpOptions2;

#define BITS_TOKEN_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetHelperTokenFlags)(T * This, DWORD flags);                                            \
    HRESULT(STDMETHODCALLTYPE *GetHelperTokenFlags)(T * This, DWORD * flags);                                          \
    HRESULT(STDMETHODCALLTYPE *SetHelperToken)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *ClearHelperToken)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *GetHelperTokenSid)(T * This, LPWSTR * sid);
BITS_INTERFACE(IBitsTokenOptions, BITS_TOKEN_METHODS)

#define BITS_CALLBACK3_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *JobTransferred)(T * This, IBackgroundCopyJob * job);                                    \
    HRESULT(STDMETHODCALLTYPE *JobError)(T * This, IBackgroundCopyJob * job, IBackgroundCopyError * error);            \
    HRESULT(STDMETHODCALLTYPE *JobModification)(T * This, IBackgroundCopyJob * job, DWORD reserved);                   \
    HRESULT(STDMETHODCALLTYPE *FileTransferred)(T * This, IBackgroundCopyJob * job, IBackgroundCopyFile * file);       \
    HRESULT(STDMETHODCALLTYPE *FileRangesTransferred)(T * This, IBackgroundCopyJob * job, IBackgroundCopyFile * file,  \
                                                      DWORD count, const BG_FILE_RANGE *ranges);
BITS_INTERFACE(IBackgroundCopyCallback3, BITS_CALLBACK3_METHODS)
typedef IBackgroundCopyCallback3 IBackgroundCopyCallback, IBackgroundCopyCallback2;

#define BITS_CERT_CALLBACK_METHODS(T)                                                                                  \
    HRESULT(STDMETHODCALLTYPE *ValidateServerCertificate)(T * This, IBackgroundCopyJob * job,                          \
                                                          IBackgroundCopyFile * file, DWORD length, const BYTE *data,  \
                                                          DWORD encoding, DWORD store_length, const BYTE *store);
BITS_INTERFACE(IBackgroundCopyServerCertificateValidationCallback, BITS_CERT_CALLBACK_METHODS)

#define BITS_MANAGER_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateJob)(T * This, LPCWSTR name, BG_JOB_TYPE type, GUID * id,                         \
                                          IBackgroundCopyJob * *job);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetJob)(T * This, REFGUID id, IBackgroundCopyJob * *job);                               \
    HRESULT(STDMETHODCALLTYPE *EnumJobs)(T * This, DWORD flags, IEnumBackgroundCopyJobs * *jobs);                      \
    HRESULT(STDMETHODCALLTYPE *GetErrorDescription)(T * This, HRESULT code, DWORD language, LPWSTR * text);
BITS_INTERFACE(IBackgroundCopyManager, BITS_MANAGER_METHODS)

MIME_DEFINE_GUID(IID_IBitsPeerCacheAdministration, 0x659CDEAD, 0x489E, 0x11D9, 0xA9, 0xCD, 0x00, 0x0D, 0x56, 0x96, 0x52,
                 0x51);

typedef struct IBitsPeerCacheRecord IBitsPeerCacheRecord;
typedef struct IEnumBitsPeerCacheRecords IEnumBitsPeerCacheRecords;
typedef struct IEnumBitsPeers IEnumBitsPeers;

#define BITS_PEER_ADMIN_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *GetMaximumCacheSize)(T * This, DWORD * bytes);                                          \
    HRESULT(STDMETHODCALLTYPE *SetMaximumCacheSize)(T * This, DWORD bytes);                                            \
    HRESULT(STDMETHODCALLTYPE *GetMaximumContentAge)(T * This, ULONG * seconds);                                       \
    HRESULT(STDMETHODCALLTYPE *SetMaximumContentAge)(T * This, ULONG seconds);                                         \
    HRESULT(STDMETHODCALLTYPE *GetConfigurationFlags)(T * This, DWORD * flags);                                        \
    HRESULT(STDMETHODCALLTYPE *SetConfigurationFlags)(T * This, DWORD flags);                                          \
    HRESULT(STDMETHODCALLTYPE *EnumRecords)(T * This, IEnumBitsPeerCacheRecords * *records);                           \
    HRESULT(STDMETHODCALLTYPE *GetRecord)(T * This, REFGUID id, IBitsPeerCacheRecord * *record);                       \
    HRESULT(STDMETHODCALLTYPE *ClearRecords)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *DeleteRecord)(T * This, REFGUID id);                                                    \
    HRESULT(STDMETHODCALLTYPE *DeleteUrl)(T * This, LPCWSTR url);                                                      \
    HRESULT(STDMETHODCALLTYPE *EnumPeers)(T * This, IEnumBitsPeers * *peers);                                          \
    HRESULT(STDMETHODCALLTYPE *ClearPeers)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *DiscoverPeers)(T * This);
BITS_INTERFACE(IBitsPeerCacheAdministration, BITS_PEER_ADMIN_METHODS)

/* ------------------------------------------------------------------ BITS 1.0, qmgr.h */

MIME_DEFINE_GUID(IID_IBackgroundCopyQMgr, 0x16F41C69, 0x09F5, 0x41D2, 0x8C, 0xD8, 0x3C, 0x08, 0xC4, 0x7B, 0xC8, 0xA8);
MIME_DEFINE_GUID(IID_IBackgroundCopyGroup, 0x1DED80A7, 0x53EA, 0x424F, 0x8A, 0x04, 0x17, 0xFE, 0xA9, 0xAD, 0xC4, 0xF5);
MIME_DEFINE_GUID(IID_IBackgroundCopyJob1, 0x59F5553C, 0x2031, 0x4629, 0xBB, 0x18, 0x26, 0x45, 0xA6, 0x97, 0x09, 0x47);
MIME_DEFINE_GUID(IID_IEnumBackgroundCopyGroups, 0xD993E603, 0x4AA4, 0x47C5, 0x86, 0x65, 0xC2, 0x0D, 0x39, 0xC2, 0xBA,
                 0x4F);
MIME_DEFINE_GUID(IID_IEnumBackgroundCopyJobs1, 0x8BAEBA9D, 0x8F1C, 0x42C4, 0xB8, 0x2C, 0x09, 0xAE, 0x79, 0x98, 0x0D,
                 0x25);
MIME_DEFINE_GUID(IID_IBackgroundCopyCallback1, 0x084F6593, 0x3800, 0x4E08, 0x9B, 0x59, 0x99, 0xFA, 0x59, 0xAD, 0xDF,
                 0x82);

#define QM_NOTIFY_FILE_DONE 0x0001
#define QM_NOTIFY_JOB_DONE 0x0002
#define QM_NOTIFY_GROUP_DONE 0x0004
#define QM_NOTIFY_DISABLE_NOTIFY 0x0040
#define QM_NOTIFY_USE_PROGRESSEX 0x0080
#define QM_STATUS_FILE_COMPLETE 0x0001
#define QM_STATUS_FILE_INCOMPLETE 0x0002
#define QM_STATUS_JOB_COMPLETE 0x0004
#define QM_STATUS_JOB_INCOMPLETE 0x0008
#define QM_STATUS_JOB_ERROR 0x0010
#define QM_STATUS_JOB_FOREGROUND 0x0020
#define QM_STATUS_GROUP_COMPLETE 0x0040
#define QM_STATUS_GROUP_INCOMPLETE 0x0080
#define QM_STATUS_GROUP_SUSPENDED 0x0100
#define QM_STATUS_GROUP_ERROR 0x0200
#define QM_STATUS_GROUP_FOREGROUND 0x0400
#define QM_PROTOCOL_HTTP 1
#define QM_PROTOCOL_FTP 2
#define QM_PROTOCOL_SMB 3
#define QM_PROTOCOL_CUSTOM 4
#define QM_PROGRESS_PERCENT_DONE 1
#define QM_PROGRESS_TIME_DONE 2
#define QM_PROGRESS_SIZE_DONE 3
#define QM_E_INVALID_STATE ((HRESULT)0x81001001L)
#define QM_E_SERVICE_UNAVAILABLE ((HRESULT)0x81001002L)
#define QM_E_DOWNLOADER_UNAVAILABLE ((HRESULT)0x81001003L)
#define QM_E_ITEM_NOT_FOUND ((HRESULT)0x81001004L)

typedef enum {
    GROUPPROP_PRIORITY,
    GROUPPROP_REMOTEUSERID,
    GROUPPROP_REMOTEUSERPWD,
    GROUPPROP_LOCALUSERID,
    GROUPPROP_LOCALUSERPWD,
    GROUPPROP_REMOTEURL,
    GROUPPROP_PROTOCOLFLAGS,
    GROUPPROP_NOTIFYFLAGS,
    GROUPPROP_NOTIFYCLSID,
    GROUPPROP_PROGRESSSIZE,
    GROUPPROP_PROGRESSPERCENT,
    GROUPPROP_PROGRESSTIME,
    GROUPPROP_DISPLAYNAME,
    GROUPPROP_DESCRIPTION,
} GROUPPROP;

typedef struct _FILESETINFO {
    BSTR bstrRemoteFile;
    BSTR bstrLocalFile;
    DWORD dwSizeHint;
} FILESETINFO;

typedef struct IBackgroundCopyGroup IBackgroundCopyGroup;
typedef struct IBackgroundCopyJob1 IBackgroundCopyJob1;
typedef struct IEnumBackgroundCopyGroups IEnumBackgroundCopyGroups;
typedef struct IEnumBackgroundCopyJobs1 IEnumBackgroundCopyJobs1;

#define BITS_JOB1_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CancelJob)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetProgress)(T * This, DWORD flags, DWORD * progress);                                  \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, DWORD * status, DWORD * win32, DWORD * transport,                  \
                                          DWORD * retries);                                                            \
    HRESULT(STDMETHODCALLTYPE *AddFiles)(T * This, ULONG count, FILESETINFO * *files);                                 \
    HRESULT(STDMETHODCALLTYPE *GetFile)(T * This, ULONG index, FILESETINFO * file);                                    \
    HRESULT(STDMETHODCALLTYPE *GetFileCount)(T * This, DWORD * count);                                                 \
    HRESULT(STDMETHODCALLTYPE *SwitchToForeground)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *get_JobID)(T * This, GUID * id);
typedef struct IBackgroundCopyJob1Vtbl {
    BITS_IUNKNOWN(IBackgroundCopyJob1)
    BITS_JOB1_METHODS(IBackgroundCopyJob1)
} IBackgroundCopyJob1Vtbl;
struct IBackgroundCopyJob1 {
    const IBackgroundCopyJob1Vtbl *lpVtbl;
};

#define BITS_GUID_ENUM_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, ULONG count, GUID * items, ULONG * fetched);                            \
    HRESULT(STDMETHODCALLTYPE *Skip)(T * This, ULONG count);                                                           \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, T * *copy);                                                            \
    HRESULT(STDMETHODCALLTYPE *GetCount)(T * This, ULONG * count);
typedef struct IEnumBackgroundCopyJobs1Vtbl {
    BITS_IUNKNOWN(IEnumBackgroundCopyJobs1)
    BITS_GUID_ENUM_METHODS(IEnumBackgroundCopyJobs1)
} IEnumBackgroundCopyJobs1Vtbl;
struct IEnumBackgroundCopyJobs1 {
    const IEnumBackgroundCopyJobs1Vtbl *lpVtbl;
};
typedef struct IEnumBackgroundCopyGroupsVtbl {
    BITS_IUNKNOWN(IEnumBackgroundCopyGroups)
    BITS_GUID_ENUM_METHODS(IEnumBackgroundCopyGroups)
} IEnumBackgroundCopyGroupsVtbl;
struct IEnumBackgroundCopyGroups {
    const IEnumBackgroundCopyGroupsVtbl *lpVtbl;
};

#define BITS_GROUP_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *GetProp)(T * This, GROUPPROP id, VARIANT * value);                                      \
    HRESULT(STDMETHODCALLTYPE *SetProp)(T * This, GROUPPROP id, VARIANT * value);                                      \
    HRESULT(STDMETHODCALLTYPE *GetProgress)(T * This, DWORD flags, DWORD * progress);                                  \
    HRESULT(STDMETHODCALLTYPE *GetStatus)(T * This, DWORD * status, DWORD * job_index);                                \
    HRESULT(STDMETHODCALLTYPE *GetJob)(T * This, GUID id, IBackgroundCopyJob1 * *job);                                 \
    HRESULT(STDMETHODCALLTYPE *SuspendGroup)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *ResumeGroup)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *CancelGroup)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, DWORD * size);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_GroupID)(T * This, GUID * id);                                                      \
    HRESULT(STDMETHODCALLTYPE *CreateJob)(T * This, GUID id, IBackgroundCopyJob1 * *job);                              \
    HRESULT(STDMETHODCALLTYPE *EnumJobs)(T * This, DWORD flags, IEnumBackgroundCopyJobs1 * *jobs);                     \
    HRESULT(STDMETHODCALLTYPE *SwitchToForeground)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *QueryNewJobInterface)(T * This, REFIID iid, IUnknown * *object);                        \
    HRESULT(STDMETHODCALLTYPE *SetNotificationPointer)(T * This, REFIID iid, IUnknown * object);
typedef struct IBackgroundCopyGroupVtbl {
    BITS_IUNKNOWN(IBackgroundCopyGroup)
    BITS_GROUP_METHODS(IBackgroundCopyGroup)
} IBackgroundCopyGroupVtbl;
struct IBackgroundCopyGroup {
    const IBackgroundCopyGroupVtbl *lpVtbl;
};

#define BITS_CALLBACK1_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *OnStatus)(T * This, IBackgroundCopyGroup * group, IBackgroundCopyJob1 * job,            \
                                         DWORD file, DWORD status, DWORD retries, DWORD win32, DWORD transport);       \
    HRESULT(STDMETHODCALLTYPE *OnProgress)(T * This, DWORD type, IBackgroundCopyGroup * group,                         \
                                           IBackgroundCopyJob1 * job, DWORD file, DWORD value);                        \
    HRESULT(STDMETHODCALLTYPE *OnProgressEx)(T * This, DWORD type, IBackgroundCopyGroup * group,                       \
                                             IBackgroundCopyJob1 * job, DWORD file, DWORD value, DWORD size,           \
                                             BYTE * data);
BITS_INTERFACE(IBackgroundCopyCallback1, BITS_CALLBACK1_METHODS)

#define BITS_QMGR_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateGroup)(T * This, GUID id, IBackgroundCopyGroup * *group);                         \
    HRESULT(STDMETHODCALLTYPE *GetGroup)(T * This, GUID id, IBackgroundCopyGroup * *group);                            \
    HRESULT(STDMETHODCALLTYPE *EnumGroups)(T * This, DWORD flags, IEnumBackgroundCopyGroups * *groups);
BITS_INTERFACE(IBackgroundCopyQMgr, BITS_QMGR_METHODS)

#endif
