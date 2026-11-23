#ifndef MIME_WIN32_DELIVERYOPTIMIZATION_H
#define MIME_WIN32_DELIVERYOPTIMIZATION_H

#include <mime/win32/oleauto.h>
#include <mime/win32/objidl.h>

MIME_DEFINE_GUID(CLSID_DeliveryOptimization, 0x5B99FA76, 0x721C, 0x423C, 0xAD, 0xAC, 0x56, 0xD0, 0x3C, 0x8A, 0x80, 0x07);
MIME_DEFINE_GUID(IID_IDOManager, 0x400E2D4A, 0x1431, 0x4C1A, 0xA7, 0x48, 0x39, 0xCA, 0x47, 0x2C, 0xFD, 0xB1);
MIME_DEFINE_GUID(IID_IDODownload, 0xFBBD7FC0, 0xC147, 0x4727, 0xA3, 0x8D, 0x82, 0x7E, 0xF0, 0x71, 0xEE, 0x77);
MIME_DEFINE_GUID(IID_IDODownloadStatusCallback, 0xD166E8E3, 0xA90E, 0x4392, 0x8E, 0x87, 0x05, 0xE9, 0x96, 0xD3, 0x74, 0x7D);
MIME_DEFINE_GUID(IID_IDODownloadInternal, 0x770B2511, 0x4ADD, 0x47F0, 0xB7, 0x5F, 0xD8, 0xE5, 0xE4, 0x77, 0x94, 0xB5);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationMgrInternal, 0x6692FD56, 0x3B9B, 0x433A, 0xAC, 0x04, 0x3F, 0xFB, 0x44, 0x25, 0x56,
                 0xDD);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationUsage, 0x42DD02AB, 0xDF13, 0x4761, 0xA5, 0xF2, 0xB9, 0xE1, 0x32, 0x64, 0xA8, 0x31);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationPolicy, 0x6A1C005C, 0xEC79, 0x4BBB, 0x88, 0xB5, 0x18, 0x8B, 0xC4, 0x56, 0x11, 0xCA);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationUsageCallback, 0x9D7DDA4D, 0xAC17, 0x4132, 0xBB, 0xB8, 0x5B, 0x8A, 0x65, 0x53, 0x87,
                 0x2F);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationUser, 0x8863F93E, 0x77EA, 0x4C67, 0xA8, 0x6F, 0x76, 0x38, 0xE3, 0xA5, 0x68, 0xA6);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationJob, 0xEE2584CF, 0xA69C, 0x4848, 0xB6, 0x33, 0x26, 0x49, 0x96, 0x2B, 0x3E, 0xF7);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationJob2, 0x18995A26, 0xBF59, 0x4ABE, 0x9F, 0x8B, 0xD5, 0x09, 0x2D, 0x5A, 0x24, 0x05);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationFile, 0xB76B9699, 0xE99E, 0x4101, 0x80, 0x3F, 0xA2, 0x0E, 0x32, 0x5D, 0x93, 0xE2);
MIME_DEFINE_GUID(IID_IDeliveryOptimizationFile2, 0x3A87296F, 0x6EC2, 0x4126, 0xAB, 0x29, 0xE3, 0xF8, 0xDC, 0x4C, 0xC3, 0x90);

#define DO_E_NO_SERVICE ((HRESULT)0x80D01001L)
#define DO_E_DOWNLOAD_NO_PROGRESS ((HRESULT)0x80D02002L)
#define DO_E_JOB_NOT_FOUND ((HRESULT)0x80D02003L)
#define DO_E_NO_DOWNLOADS ((HRESULT)0x80D02005L)
#define DO_E_JOB_TOO_OLD ((HRESULT)0x80D0200CL)
#define DO_E_UNKNOWN_PROPERTY_ID ((HRESULT)0x80D02011L)
#define DO_E_READ_ONLY_PROPERTY ((HRESULT)0x80D02012L)
#define DO_E_INVALID_STATE ((HRESULT)0x80D02013L)
#define DO_E_WRITE_ONLY_PROPERTY ((HRESULT)0x80D02015L)
#define DO_E_FILE_DOWNLOADSINK_UNSPECIFIED ((HRESULT)0x80D02018L)
#define DO_E_DOWNLOAD_NO_URI ((HRESULT)0x80D02200L)
#define DO_E_BLOCKED_BY_COST_TRANSFER_POLICY ((HRESULT)0x80D03801L)
#define DO_E_BLOCKED_BY_NO_NETWORK ((HRESULT)0x80D03805L)
#define DO_E_HTTP_BLOCKSIZE_MISMATCH ((HRESULT)0x80D05001L)
#define DO_E_INVALID_RANGE ((HRESULT)0x80D05002L)
#define DO_E_INSUFFICIENT_RANGE_SUPPORT ((HRESULT)0x80D05003L)
#define DO_E_OVERLAPPING_RANGES ((HRESULT)0x80D05004L)

#define DO_LENGTH_TO_EOF ((UINT64)-1)

typedef enum _DODownloadState {
    DODownloadState_Created = 0,
    DODownloadState_Transferring = 1,
    DODownloadState_Transferred = 2,
    DODownloadState_Finalized = 3,
    DODownloadState_Aborted = 4,
    DODownloadState_Paused = 5,
} DODownloadState;

typedef enum _DODownloadCostPolicy {
    DODownloadCostPolicy_Always = 0,
    DODownloadCostPolicy_Unrestricted = 1,
    DODownloadCostPolicy_Standard = 2,
    DODownloadCostPolicy_NoRoaming = 3,
    DODownloadCostPolicy_NoSurcharge = 4,
    DODownloadCostPolicy_NoCellular = 5,
} DODownloadCostPolicy;

typedef enum _DODownloadProperty {
    DODownloadProperty_Id = 0,
    DODownloadProperty_Uri = 1,
    DODownloadProperty_ContentId = 2,
    DODownloadProperty_DisplayName = 3,
    DODownloadProperty_LocalPath = 4,
    DODownloadProperty_HttpCustomHeaders = 5,
    DODownloadProperty_CostPolicy = 6,
    DODownloadProperty_SecurityFlags = 7,
    DODownloadProperty_CallbackFreqPercent = 8,
    DODownloadProperty_CallbackFreqSeconds = 9,
    DODownloadProperty_NoProgressTimeoutSeconds = 10,
    DODownloadProperty_ForegroundPriority = 11,
    DODownloadProperty_BlockingMode = 12,
    DODownloadProperty_CallbackInterface = 13,
    DODownloadProperty_StreamInterface = 14,
    DODownloadProperty_SecurityContext = 15,
    DODownloadProperty_NetworkToken = 16,
    DODownloadProperty_CorrelationVector = 17,
    DODownloadProperty_DecryptionInfo = 18,
    DODownloadProperty_IntegrityCheckInfo = 19,
    DODownloadProperty_IntegrityCheckMandatory = 20,
    DODownloadProperty_TotalSizeBytes = 21,
    DODownloadProperty_DisallowOnCellular = 22,
    DODownloadProperty_HttpCustomAuthHeaders = 23,
    DODownloadProperty_HttpAllowSecureToNonSecureRedirect = 24,
    DODownloadProperty_NonVolatile = 25,
    DODownloadProperty_HttpRedirectionTarget = 26,
    DODownloadProperty_HttpResponseHeaders = 27,
    DODownloadProperty_HttpServerIPAddress = 28,
    DODownloadProperty_HttpStatusCode = 29,
} DODownloadProperty;

/* the ids of IDODownloadInternal, measured against 26300: 1 is a correlation vector, 4 and 6 are flags. */
typedef enum _DODownloadPropertyEx {
    DODownloadPropertyEx_Count = 7,
} DODownloadPropertyEx;

typedef enum _DeliveryOptimizationJobProperty {
    DeliveryOptimizationJobProperty_Count = 0,
} DeliveryOptimizationJobProperty;

typedef enum _DeliveryOptimizationFileProperty {
    DeliveryOptimizationFileProperty_Count = 0,
} DeliveryOptimizationFileProperty;

typedef struct _DO_DOWNLOAD_RANGE {
    UINT64 Offset;
    UINT64 Length;
} DO_DOWNLOAD_RANGE;

typedef struct _DO_DOWNLOAD_RANGES_INFO {
    UINT RangeCount;
    DO_DOWNLOAD_RANGE Ranges[1];
} DO_DOWNLOAD_RANGES_INFO;

typedef struct _DO_DOWNLOAD_STATUS {
    UINT64 BytesTotal;
    UINT64 BytesTransferred;
    DODownloadState State;
    HRESULT Error;
    HRESULT ExtendedError;
} DO_DOWNLOAD_STATUS;

typedef struct _DO_DOWNLOAD_ENUM_CATEGORY {
    DODownloadProperty Property;
    LPCWSTR Value;
} DO_DOWNLOAD_ENUM_CATEGORY;

#ifndef MIME_BG_FILE_RANGE_DEFINED
#define MIME_BG_FILE_RANGE_DEFINED
typedef struct _BG_FILE_RANGE {
    UINT64 InitialOffset;
    UINT64 Length;
} BG_FILE_RANGE;
#endif

typedef struct _DOUsagePolicy {
    ULONG Values[5];
} DOUsagePolicy;

typedef struct _DOUsageStats {
    ULONG Values[6];
} DOUsageStats;

typedef struct _DOSwarmStats {
    LPWSTR Id;
    LPWSTR Name;
    UINT64 Bytes[11];
    ULONG Counts[8];
} DOSwarmStats;

#define MIME_DO_IUNKNOWN(name)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(name * This, REFIID riid, void **object);                               \
    ULONG(STDMETHODCALLTYPE *AddRef)(name * This);                                                                     \
    ULONG(STDMETHODCALLTYPE *Release)(name * This);

typedef struct IDODownload IDODownload;
typedef struct IDODownloadVtbl {
    MIME_DO_IUNKNOWN(IDODownload)
    HRESULT(STDMETHODCALLTYPE *Start)(IDODownload *This, const DO_DOWNLOAD_RANGES_INFO *ranges);
    HRESULT(STDMETHODCALLTYPE *Pause)(IDODownload *This);
    HRESULT(STDMETHODCALLTYPE *Abort)(IDODownload *This);
    HRESULT(STDMETHODCALLTYPE *Finalize)(IDODownload *This);
    HRESULT(STDMETHODCALLTYPE *GetStatus)(IDODownload *This, DO_DOWNLOAD_STATUS *status);
    HRESULT(STDMETHODCALLTYPE *GetProperty)(IDODownload *This, DODownloadProperty propId, VARIANT *propVal);
    HRESULT(STDMETHODCALLTYPE *SetProperty)(IDODownload *This, DODownloadProperty propId, const VARIANT *propVal);
} IDODownloadVtbl;
struct IDODownload {
    const IDODownloadVtbl *lpVtbl;
};

typedef struct IDOManager IDOManager;
typedef struct IDOManagerVtbl {
    MIME_DO_IUNKNOWN(IDOManager)
    HRESULT(STDMETHODCALLTYPE *CreateDownload)(IDOManager *This, IDODownload **download);
    HRESULT(STDMETHODCALLTYPE *EnumDownloads)(IDOManager *This, const DO_DOWNLOAD_ENUM_CATEGORY *category,
                                              IEnumUnknown **ppEnum);
} IDOManagerVtbl;
struct IDOManager {
    const IDOManagerVtbl *lpVtbl;
};

typedef struct IDODownloadStatusCallback IDODownloadStatusCallback;
typedef struct IDODownloadStatusCallbackVtbl {
    MIME_DO_IUNKNOWN(IDODownloadStatusCallback)
    HRESULT(STDMETHODCALLTYPE *OnStatusChange)(IDODownloadStatusCallback *This, IDODownload *download,
                                               const DO_DOWNLOAD_STATUS *status);
} IDODownloadStatusCallbackVtbl;
struct IDODownloadStatusCallback {
    const IDODownloadStatusCallbackVtbl *lpVtbl;
};

typedef struct IDODownloadInternal IDODownloadInternal;
typedef struct IDODownloadInternalVtbl {
    MIME_DO_IUNKNOWN(IDODownloadInternal)
    HRESULT(STDMETHODCALLTYPE *GetPropertyEx)(IDODownloadInternal *This, DODownloadPropertyEx propId, VARIANT *propVal);
    HRESULT(STDMETHODCALLTYPE *SetPropertyEx)(IDODownloadInternal *This, DODownloadPropertyEx propId,
                                              const VARIANT *propVal);
} IDODownloadInternalVtbl;
struct IDODownloadInternal {
    const IDODownloadInternalVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationMgrInternal IDeliveryOptimizationMgrInternal;
typedef struct IDeliveryOptimizationMgrInternalVtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationMgrInternal)
    HRESULT(STDMETHODCALLTYPE *SetUserToken)(IDeliveryOptimizationMgrInternal *This);
    HRESULT(STDMETHODCALLTYPE *GetCacheSize)(IDeliveryOptimizationMgrInternal *This, UINT64 *size);
    HRESULT(STDMETHODCALLTYPE *DeleteCache)(IDeliveryOptimizationMgrInternal *This);
    HRESULT(STDMETHODCALLTYPE *SetSwarmPinned)(IDeliveryOptimizationMgrInternal *This, const WCHAR *id, LONG pinned);
    HRESULT(STDMETHODCALLTYPE *SetExpiration)(IDeliveryOptimizationMgrInternal *This, UINT64 expiration, const WCHAR *id);
    HRESULT(STDMETHODCALLTYPE *DeleteSwarms)(IDeliveryOptimizationMgrInternal *This, const WCHAR *id, LONG all);
} IDeliveryOptimizationMgrInternalVtbl;
struct IDeliveryOptimizationMgrInternal {
    const IDeliveryOptimizationMgrInternalVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationUsageCallback IDeliveryOptimizationUsageCallback;
typedef struct IDeliveryOptimizationUsageCallbackVtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationUsageCallback)
    HRESULT(STDMETHODCALLTYPE *SendUsage)(IDeliveryOptimizationUsageCallback *This, const DOUsageStats *stats);
    HRESULT(STDMETHODCALLTYPE *SendIdle)(IDeliveryOptimizationUsageCallback *This);
} IDeliveryOptimizationUsageCallbackVtbl;
struct IDeliveryOptimizationUsageCallback {
    const IDeliveryOptimizationUsageCallbackVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationUsage IDeliveryOptimizationUsage;
typedef struct IDeliveryOptimizationUsageVtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationUsage)
    HRESULT(STDMETHODCALLTYPE *Advise)(IDeliveryOptimizationUsage *This, IDeliveryOptimizationUsageCallback *callback);
    HRESULT(STDMETHODCALLTYPE *Unadvise)(IDeliveryOptimizationUsage *This, IDeliveryOptimizationUsageCallback *callback);
} IDeliveryOptimizationUsageVtbl;
struct IDeliveryOptimizationUsage {
    const IDeliveryOptimizationUsageVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationPolicy IDeliveryOptimizationPolicy;
typedef struct IDeliveryOptimizationPolicyVtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationPolicy)
    HRESULT(STDMETHODCALLTYPE *SetUsagePolicy)(IDeliveryOptimizationPolicy *This, const DOUsagePolicy *policy);
    HRESULT(STDMETHODCALLTYPE *GetUsagePolicy)(IDeliveryOptimizationPolicy *This, DOUsagePolicy *policy);
    HRESULT(STDMETHODCALLTYPE *GetUsageStats)(IDeliveryOptimizationPolicy *This, DOUsageStats *stats);
} IDeliveryOptimizationPolicyVtbl;
struct IDeliveryOptimizationPolicy {
    const IDeliveryOptimizationPolicyVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationUser IDeliveryOptimizationUser;
typedef struct IDeliveryOptimizationUserVtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationUser)
    HRESULT(STDMETHODCALLTYPE *SetUser)(IDeliveryOptimizationUser *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *ClearUser)(IDeliveryOptimizationUser *This);
} IDeliveryOptimizationUserVtbl;
struct IDeliveryOptimizationUser {
    const IDeliveryOptimizationUserVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationJob IDeliveryOptimizationJob;
typedef struct IDeliveryOptimizationJobVtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationJob)
    HRESULT(STDMETHODCALLTYPE *AddFileWithRanges)(IDeliveryOptimizationJob *This, const WCHAR *remoteUrl, const WCHAR *id,
                                                  const WCHAR *localName, ULONG rangeCount, BG_FILE_RANGE *ranges,
                                                  UINT64 size);
    HRESULT(STDMETHODCALLTYPE *SetUpdateId)(IDeliveryOptimizationJob *This, const WCHAR *id);
    HRESULT(STDMETHODCALLTYPE *SetServerId)(IDeliveryOptimizationJob *This, const WCHAR *id);
    HRESULT(STDMETHODCALLTYPE *SetFlags)(IDeliveryOptimizationJob *This, ULONG flags);
    HRESULT(STDMETHODCALLTYPE *SetCorrelationVector)(IDeliveryOptimizationJob *This, const char *cv);
} IDeliveryOptimizationJobVtbl;
struct IDeliveryOptimizationJob {
    const IDeliveryOptimizationJobVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationJob2 IDeliveryOptimizationJob2;
typedef struct IDeliveryOptimizationJob2Vtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationJob2)
    HRESULT(STDMETHODCALLTYPE *AddFile)(IDeliveryOptimizationJob2 *This, const WCHAR *remoteUrl, const WCHAR *localName,
                                        ULONG rangeCount, BG_FILE_RANGE *ranges, REFIID riid, void **file);
    HRESULT(STDMETHODCALLTYPE *GetProperty)(IDeliveryOptimizationJob2 *This, DeliveryOptimizationJobProperty propId,
                                            VARIANT *propVal);
    HRESULT(STDMETHODCALLTYPE *SetProperty)(IDeliveryOptimizationJob2 *This, DeliveryOptimizationJobProperty propId,
                                            const VARIANT *propVal);
} IDeliveryOptimizationJob2Vtbl;
struct IDeliveryOptimizationJob2 {
    const IDeliveryOptimizationJob2Vtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationFile IDeliveryOptimizationFile;
typedef struct IDeliveryOptimizationFileVtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationFile)
    HRESULT(STDMETHODCALLTYPE *GetId)(IDeliveryOptimizationFile *This, LPWSTR *id);
    HRESULT(STDMETHODCALLTYPE *GetStats)(IDeliveryOptimizationFile *This, DOSwarmStats *stats);
} IDeliveryOptimizationFileVtbl;
struct IDeliveryOptimizationFile {
    const IDeliveryOptimizationFileVtbl *lpVtbl;
};

typedef struct IDeliveryOptimizationFile2 IDeliveryOptimizationFile2;
typedef struct IDeliveryOptimizationFile2Vtbl {
    MIME_DO_IUNKNOWN(IDeliveryOptimizationFile2)
    HRESULT(STDMETHODCALLTYPE *GetProperty)(IDeliveryOptimizationFile2 *This, DeliveryOptimizationFileProperty propId,
                                            VARIANT *propVal);
    HRESULT(STDMETHODCALLTYPE *SetProperty)(IDeliveryOptimizationFile2 *This, DeliveryOptimizationFileProperty propId,
                                            const VARIANT *propVal);
} IDeliveryOptimizationFile2Vtbl;
struct IDeliveryOptimizationFile2 {
    const IDeliveryOptimizationFile2Vtbl *lpVtbl;
};

#endif
