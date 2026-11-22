#ifndef MIME_DOSVC_H
#define MIME_DOSVC_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/deliveryoptimization.h>
#include <mime/win32/wincrypt.h>
#include "transfer/transfer.h"
#include "transfer/caller.h"

/* the states of a job, which the public interface folds: the three pauses all read as DODownloadState_Paused. */
enum {
    JOB_CREATED,
    JOB_TRANSFERRING,
    JOB_TRANSFERRED,
    JOB_FINALIZED,
    JOB_ABORTED,
    JOB_PAUSED,
    JOB_ERROR,
    JOB_TRANSIENT,
};

/* the property ids of the job, which the public ids map to: 24 and 25 are reached through IDODownloadInternal only. */
enum {
    P_ID,
    P_URI,
    P_CONTENT_ID,
    P_DISPLAY_NAME,
    P_LOCAL_PATH,
    P_HEADERS,
    P_COST,
    P_SECURITY_FLAGS,
    P_FREQ_PERCENT,
    P_FREQ_SECONDS,
    P_NO_PROGRESS,
    P_FOREGROUND,
    P_BLOCKING,
    P_CALLBACK,
    P_STREAM,
    P_SECURITY_CONTEXT,
    P_NETWORK_TOKEN,
    P_CV,
    P_DECRYPTION,
    P_INTEGRITY,
    P_INTEGRITY_MANDATORY,
    P_TOTAL_SIZE,
    P_NO_CELLULAR,
    P_AUTH_HEADERS,
    P_UPDATE_ID,
    P_TEMP_FILE,
    P_INSECURE_REDIRECT,
    P_NON_VOLATILE,
    P_REDIRECT_TARGET,
    P_RESPONSE_HEADERS,
    P_SERVER_ADDRESS,
    P_STATUS_CODE,
    P_COUNT,
};

#define DO_E_NO_FILE_ ((HRESULT)0x80D02004L)
#define DO_E_SINK_SET_ ((HRESULT)0x80D02019L)
#define DO_E_RANGES_NOT_NOW_ ((HRESULT)0x80D02203L)
#define DO_E_RANGES_ALREADY_ ((HRESULT)0x80D02204L)
#define DO_E_SIZE_MISMATCH_ ((HRESULT)0x80D05001L)
#define DO_E_FATAL_FILE_ ((HRESULT)0x80D05002L)
#define DO_E_INVALID_RANGE_ ((HRESULT)0x80D05010L)
#define DO_E_NO_RANGE_SUPPORT_ ((HRESULT)0x80D05011L)
#define DO_E_OVERLAPPING_RANGES_ ((HRESULT)0x80D05012L)
#define DO_E_UNEXPECTED_STATUS_ ((HRESULT)0x80D0380DL)
#define CLASS_E_NOTAVAILABLE_ ((HRESULT)0x80040111L)

typedef struct DO_EXT DO_EXT;

typedef struct DO_JOB {
    struct DO_JOB *next;
    LONG refs;
    SRWLOCK lock;
    GUID id;
    int state;
    BOOL finalizing;
    BOOL bits;
    HRESULT error;
    HRESULT ext_error;
    VARIANT props[P_COUNT];
    WCHAR *owner_sid;
    HANDLE owner_token;
    /* the correlation vector: a base of 16 characters and the counter after it. */
    WCHAR cv_base[48];
    ULONG cv_counter;
    DO_DOWNLOAD_RANGE *ranges;
    UINT range_count;
    BOOL ranges_queued;
    BOOL has_file;
    WCHAR *remote;
    WCHAR *local;
    WCHAR *temp;
    UINT64 total;
    UINT64 transferred;
    BOOL size_known;
    UINT64 probed;
    DO_DOWNLOAD_STATUS notify_status;
    ISequentialStream *stream;
    PCCERT_CONTEXT client_cert;
    HANDLE worker;
    HANDLE cancel;
    HANDLE blocking;
    ULONGLONG progress_tick;
    ULONGLONG notify_tick;
    UINT64 notify_bytes;
    BOOL notify_first;
    LONG notify_pending;
    DO_EXT *ext;
    FILETIME created;
} DO_JOB;

/* service.c */
void DoLockService(BOOL lock);
BOOL DoServiceRunning(void);

/* job.c */
HRESULT JobCreate(DO_JOB **out);
void JobAddRef(DO_JOB *job);
void JobRelease(DO_JOB *job);
HRESULT JobStart(DO_JOB *job);
HRESULT JobSetRanges(DO_JOB *job, const DO_DOWNLOAD_RANGE *ranges, UINT count);
HRESULT JobPause(DO_JOB *job);
HRESULT JobAbort(DO_JOB *job);
HRESULT JobFinalize(DO_JOB *job);
void JobStatus(DO_JOB *job, DO_DOWNLOAD_STATUS *status);
HRESULT JobGetProperty(DO_JOB *job, int id, VARIANT *value);
HRESULT JobSetProperty(DO_JOB *job, int id, const VARIANT *value);
HRESULT JobSetStream(DO_JOB *job, ISequentialStream *stream);
void JobSetClientCertificate(DO_JOB *job, PCCERT_CONTEXT cert);
BOOL JobVisible(DO_JOB *job);
BOOL JobMatches(DO_JOB *job, int id, const WCHAR *value);
DO_JOB *JobList(void);
void JobListLock(BOOL exclusive);
void JobListUnlock(BOOL exclusive);
BOOL JobsIdle(void);
void JobShutdown(void);

/* download.c */
HRESULT ExtFor(DO_JOB *job, IDODownload **out);
void ExtNotify(DO_JOB *job);

/* manager.c */
HRESULT ManagerCreate(REFIID iid, void **out);

/* store.c */
HKEY StoreRoot(void);
void StoreSaveJob(DO_JOB *job);
void StoreDeleteJob(DO_JOB *job);
void StoreLoadJobs(void);

#endif
