#ifndef MIME_QMGR_H
#define MIME_QMGR_H

#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/rpc.h>
#include <mime/win32/bits.h>
#include "transfer/transfer.h"
#include "transfer/caller.h"
#include <mime/ntrtlsec.h>

#ifndef REGCLS_SUSPENDED
#define REGCLS_SUSPENDED 4
#endif
#define EOAC_STATIC_CLOAKING_ 0x20
#define CO_E_SERVER_STOPPING_ ((HRESULT)0x80080008L)
#define CLASS_E_NOAGGREGATION_ ((HRESULT)0x80040110L)
#define E_PENDING_ ((HRESULT)0x8000000AL)
#define RPC_E_DISCONNECTED_ ((HRESULT)0x80010108L)
#define BG_E_UPNP_ERROR ((HRESULT)0x80200045L)
#define BG_E_HTTP_ERROR_BASE ((HRESULT)0x80190000L)
#define ERROR_NOT_LOGGED_ON_ 1245
#define ERROR_NOT_READY_ 21
#define TOKEN_ALL_ACCESS_ 0xF01FF
#define TOKEN_ADJUST_DEFAULT_ 0x0080
#define TOKEN_ASSIGN_PRIMARY_ 0x0001
#define TOKEN_ADJUST_SESSIONID_ 0x0100
#define TOKEN_GROUPS_CLASS_ 2
#define TOKEN_TYPE_CLASS_ 8
#define TOKEN_SESSION_ID_CLASS_ 12
#define TOKEN_LOGON_SID_CLASS_ 28
#define TOKEN_PRIMARY_ 1
#define TOKEN_IMPERSONATION_ 2
#define SECURITY_IDENTIFICATION_ 1
#define SECURITY_DELEGATION_ 3
#define ELEVATION_LIMITED_ 3
#define SE_GROUP_ENABLED_ 0x4
#define SE_GROUP_USE_FOR_DENY_ONLY_ 0x10
#define MAXIMUM_ALLOWED_ 0x02000000
#define BITS_READ 1
#define BITS_WRITE 2
#define BITS_ALL 3
#define JOB_ACCESS_ALL 0xF003F
#define QM_ADDREF(p) ((IUnknown *)(p))->lpVtbl->AddRef((IUnknown *)(p))
#define QM_RELEASE(p) ((IUnknown *)(p))->lpVtbl->Release((IUnknown *)(p))

WINBASEAPI BOOL WINAPI ImpersonateSelf(DWORD level);
WINBASEAPI BOOL WINAPI RevertToSelf(void);
WINBASEAPI BOOL WINAPI AccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, void *mapping, void *privileges,
                                   DWORD *privileges_length, DWORD *granted, BOOL *status);
WINBASEAPI BOOL WINAPI IsTokenRestricted(HANDLE token);
WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);
WINBASEAPI BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorW(PSECURITY_DESCRIPTOR sd, DWORD revision,
                                                                            DWORD information, LPWSTR *text,
                                                                            PULONG length);
WINBASEAPI BOOL WINAPI GetFileSecurityW(LPCWSTR name, DWORD information, PSECURITY_DESCRIPTOR sd, DWORD length,
                                        DWORD *needed);
WINBASEAPI BOOL WINAPI SetFileSecurityW(LPCWSTR name, DWORD information, PSECURITY_DESCRIPTOR sd);
WINBASEAPI DWORD WINAPI GetLengthSid(PSID sid);
WINBASEAPI BOOL WINAPI EqualSid(PSID a, PSID b);
WINBASEAPI BOOL WINAPI IsValidSid(PSID sid);
WINBASEAPI BOOL WINAPI CopySid(DWORD length, PSID destination, PSID source);
WINBASEAPI LONG WINAPI GetPackageFullNameFromToken(HANDLE token, UINT32 *length, PWSTR name);
WINBASEAPI LSTATUS WINAPI RegSetKeySecurity(HKEY key, DWORD information, PSECURITY_DESCRIPTOR sd);

/* the global interface table of combase, which interfaces from other apartments and processes are kept in. */
typedef struct IGlobalInterfaceTable IGlobalInterfaceTable;
typedef struct IGlobalInterfaceTableVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IGlobalInterfaceTable *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IGlobalInterfaceTable *This);
    ULONG(STDMETHODCALLTYPE *Release)(IGlobalInterfaceTable *This);
    HRESULT(STDMETHODCALLTYPE *RegisterInterfaceInGlobal)(IGlobalInterfaceTable *This, IUnknown *object, REFIID riid,
                                                          DWORD *cookie);
    HRESULT(STDMETHODCALLTYPE *RevokeInterfaceFromGlobal)(IGlobalInterfaceTable *This, DWORD cookie);
    HRESULT(STDMETHODCALLTYPE *GetInterfaceFromGlobal)(IGlobalInterfaceTable *This, DWORD cookie, REFIID riid,
                                                       void **object);
} IGlobalInterfaceTableVtbl;
struct IGlobalInterfaceTable {
    const IGlobalInterfaceTableVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ configuration, config.c */

typedef struct QM_CONFIG {
    DWORD retry_delay;
    DWORD no_progress;
    ULONGLONG inactivity;
    DWORD quantum;
    DWORD buffer_size;
    DWORD idle_stop_ms;
    BOOL low_priority_tcp;
    BOOL limits;
    DWORD max_jobs_user;
    DWORD max_jobs_machine;
    DWORD max_files;
    DWORD max_ranges;
    DWORD max_download_time;
    DWORD cost_default[4];
    BOOL bandwidth;
    DWORD rate_on;
    DWORD rate_off;
    DWORD window_from;
    DWORD window_to;
    BOOL setup_in_progress;
} QM_CONFIG;

extern QM_CONFIG g_config;

void ConfigLoad(void);
void ConfigLoadPolicy(void);
HKEY ConfigPolicyKey(void);
DWORD ConfigRateLimit(void);
void ConfigSetDword(const WCHAR *name, DWORD value);
DWORD ConfigGetDword(const WCHAR *name, DWORD fallback);

/* ------------------------------------------------------------------ the caller, security.c */

typedef struct CALLER {
    HANDLE token;
    BYTE sid_buffer[SECURITY_MAX_SID_SIZE];
    PSID sid;
    DWORD level;
    DWORD elevated;
    DWORD elevation_type;
    DWORD il;
    DWORD session;
    BOOL admin;
    BOOL service;
    BOOL restricted;
    BOOL network;
    BOOL remote_admin;
    BOOL anonymous;
    WCHAR package[128];
} CALLER;

HRESULT CallerOpen(CALLER *caller);
void CallerClose(CALLER *caller);
HRESULT CallerCheckManager(CALLER *caller);
HANDLE CallerOwnerToken(const CALLER *caller);
BOOL SidIsService(PSID sid);
HRESULT JobSecurityCreate(PSID owner, DWORD il, PSECURITY_DESCRIPTOR *sd);
HANDLE SessionTokenFor(PSID sid);
BOOL OwnerLoggedOn(PSID sid);
void SecurityShutdown(void);

/* ------------------------------------------------------------------ the error of a job */

enum { SOURCE_HRESULT, SOURCE_WIN32, SOURCE_LEGACY, SOURCE_HTTP };

typedef struct BERROR {
    BOOL present;
    BG_ERROR_CONTEXT context;
    HRESULT code;
    DWORD source;
    LONG file;
} BERROR;

/* ------------------------------------------------------------------ files and jobs */

typedef struct BJOB BJOB;
typedef struct BFILE BFILE;
typedef struct RUN RUN;

typedef struct RANGES {
    BG_FILE_RANGE *items;
    DWORD count;
    DWORD capacity;
} RANGES;

struct BFILE {
    BJOB *job;
    DWORD index;
    WCHAR *remote;
    WCHAR *local;
    WCHAR *temp;
    UINT64 total;
    UINT64 transferred;
    UINT64 size;
    BOOL completed;
    BOOL moved;
    BOOL headed;
    BOOL resumable;
    BOOL done;
    BOOL told;
    BOOL valid;
    BOOL peer;
    BG_FILE_RANGE *ranges;
    DWORD range_count;
    RANGES filled;
    RANGES wanted;
    RANGES unreported;
    UINT64 position;
    WCHAR last_modified[64];
    WCHAR *response;
    UINT64 upload_size;
    FILETIME upload_time;
    DWORD attributes;
    void *ext;
};

typedef struct BCRED {
    BG_AUTH_TARGET target;
    BG_AUTH_SCHEME scheme;
    WCHAR *user;
    BYTE *secret;
    DWORD secret_size;
} BCRED;

#define MAX_CREDS 10

/* why a callback is due: the job event it waits to tell, given once, as the field +292 of the genuine CJob. */
enum { PENDING_NONE, PENDING_TRANSFERRED, PENDING_ERROR };

struct BJOB {
    BJOB *prev;
    BJOB *next;
    LONG refs;
    BOOL listed;
    BOOL online;
    UINT64 order;
    GUID id;
    BG_JOB_TYPE type;
    BG_JOB_STATE state;
    BG_JOB_PRIORITY priority;
    WCHAR *name;
    WCHAR *description;
    BYTE owner_buffer[SECURITY_MAX_SID_SIZE];
    PSID owner;
    WCHAR *owner_text;
    WCHAR *package;
    DWORD owner_elevated;
    DWORD owner_il;
    HANDLE owner_token;
    PSECURITY_DESCRIPTOR sd;
    ULONG notify_flags;
    DWORD git_cookie;
    int callback_version;
    GUID notify_clsid;
    WCHAR *cmd_program;
    WCHAR *cmd_params;
    ULONG retry_delay;
    ULONG no_progress;
    ULONG max_time;
    BG_JOB_PROXY_USAGE proxy_usage;
    WCHAR *proxy_list;
    WCHAR *proxy_bypass;
    ULONG error_count;
    ULONG retry_count;
    BERROR error;
    FILETIME created;
    FILETIME modified;
    FILETIME completed;
    volatile LONG64 accessed;
    UINT64 retry_at;
    UINT64 no_progress_at;
    UINT64 callback_at;
    UINT64 modification_at;
    UINT64 quantum_at;
    UINT64 elapsed_ms;
    ULONGLONG run_tick;
    BFILE **files;
    DWORD file_count;
    DWORD file_capacity;
    DWORD files_done;
    BCRED creds[MAX_CREDS];
    DWORD cred_count;
    DWORD acl_flags;
    DWORD peer_flags;
    DWORD security_flags;
    WCHAR *headers;
    BOOL headers_write_only;
    WCHAR *method;
    BOOL custom_method;
    DWORD cost_flags;
    BOOL cost_explicit;
    DWORD notify_interval;
    BOOL dynamic;
    BOOL high_performance;
    UINT64 max_size;
    DWORD power_flags;
    DWORD stored_creds;
    DWORD media_flags;
    BOOL on_demand;
    BOOL resumed;
    BOOL ranges_added;
    DWORD validator_cookie;
    BOOL has_validator;
    BOOL has_cert;
    BG_CERT_STORE_LOCATION cert_location;
    WCHAR *cert_store;
    BOOL cert_by_hash;
    BYTE cert_hash[20];
    WCHAR *cert_subject;
    BYTE (*trusted)[20];
    DWORD trusted_count;
    HANDLE helper_token;
    DWORD helper_flags;
    WCHAR *helper_sid;
    WCHAR *reply_name;
    BOOL reply_generated;
    BOOL reply_started;
    UINT64 reply_total;
    UINT64 reply_done;
    WCHAR *reply_url;
    WCHAR *session_id;
    UINT64 upload_acked;
    BOOL legacy;
    BOOL legacy_job;
    GUID legacy_job_id;
    GUID legacy_clsid;
    IUnknown *legacy_callback;
    DWORD pending_reason;
    BOOL modification_due;
    BOOL ranges_due;
    BOOL callback_busy;
    BOOL callback_queued;
    BOOL callback_hold;
    ULONGLONG last_modification_tick;
    BOOL removed;
    BOOL holding;
    RUN *run;
    void *ext;
    void *group_ext;
    void *job1_ext;
    ULONGLONG save_tick;
};

/* a transfer worker of a job: the job keeps one until it ended, and the worker reads stop under the lock. */
struct RUN {
    LONG refs;
    BJOB *job;
    HANDLE thread;
    HANDLE cancel;
    HINTERNET volatile active;
    BOOL stop;
};

/* ------------------------------------------------------------------ the manager lock and the lists, job.c */

extern SRWLOCK g_lock;
extern BJOB g_online;
extern BJOB g_offline;

void Lock(void);
void Unlock(void);
void LockShared(void);
void UnlockShared(void);

WCHAR *QmDup(const WCHAR *text);
WCHAR *QmTaskDup(const WCHAR *text);
void QmReplace(WCHAR **slot, const WCHAR *text);
UINT64 QmNow(void);
FILETIME QmFileTime(UINT64 value);
UINT64 QmTime(const FILETIME *time);
BOOL QmRangesAdd(RANGES *set, UINT64 offset, UINT64 length);
UINT64 QmRangesCovered(const RANGES *set, UINT64 offset, UINT64 end);
void QmRangesFree(RANGES *set);

enum { PROTOCOL_NONE, PROTOCOL_HTTP, PROTOCOL_HTTPS, PROTOCOL_FILE };
int QmProtocol(const WCHAR *remote);
WCHAR *QmFilePath(const WCHAR *remote);

void JobAddRef(BJOB *job);
void JobRelease(BJOB *job);
BJOB *JobFind(const GUID *id);
HRESULT JobCreate(const CALLER *caller, const WCHAR *name, BG_JOB_TYPE type, const GUID *id, BJOB **out);
void JobLink(BJOB *job, BOOL online);
void JobUnlink(BJOB *job);
void JobRemove(BJOB *job);
void JobSetState(BJOB *job, BG_JOB_STATE state);
void JobTouch(BJOB *job, int behaviour);
void JobAccessed(BJOB *job);
HRESULT JobCheckAccess(BJOB *job, const CALLER *caller, DWORD access, BOOL *owner);
BOOL JobIsOwner(BJOB *job, const CALLER *caller);
BOOL JobRunnable(BJOB *job);
BOOL JobAllDone(BJOB *job);
void JobProgress(BJOB *job, BG_JOB_PROGRESS *progress);
void JobSetError(BJOB *job, BG_ERROR_CONTEXT context, HRESULT code, DWORD source, LONG file);
void JobTransientError(BJOB *job, BG_ERROR_CONTEXT context, HRESULT code, DWORD source, LONG file);
void JobFatalError(BJOB *job, BG_ERROR_CONTEXT context, HRESULT code, DWORD source, LONG file);
void JobTransferred(BJOB *job);
HRESULT JobAddFile(BJOB *job, const CALLER *caller, const WCHAR *remote, const WCHAR *local, DWORD count,
                   const BG_FILE_RANGE *ranges);
HRESULT JobAddFileSet(BJOB *job, const CALLER *caller, ULONG count, const BG_FILE_INFO *files);
HRESULT JobResume(BJOB *job);
HRESULT JobSuspend(BJOB *job);
HRESULT JobCancel(BJOB *job, const CALLER *caller);
HRESULT JobComplete(BJOB *job);
HRESULT JobTakeOwnership(BJOB *job, const CALLER *caller);
void JobDefaultCost(BJOB *job);
HRESULT JobDeleteTempFiles(BJOB *job);
BOOL JobAsOwner(BJOB *job, BOOL files);
void JobBackToSelf(BOOL impersonating);
DWORD JobCountForOwner(PSID sid);
DWORD JobCountAll(void);
void JobUserLogon(PSID sid);
void JobUserLogoff(PSID sid);
void JobShutdown(void);

/* ------------------------------------------------------------------ files, file.c */

BFILE *FileCreate(BJOB *job, const WCHAR *remote, const WCHAR *local, DWORD count, const BG_FILE_RANGE *ranges);
void FileFree(BFILE *file);
HRESULT FileVerifyRemote(BJOB *job, const WCHAR *remote, BOOL ranged);
HRESULT FileVerifyLocal(BFILE *file, const WCHAR *local);
HRESULT FileCreateTemp(BFILE *file);
HRESULT FileDeleteTemp(BFILE *file);
HRESULT FileCommit(BFILE *file);
void FileProgress(BFILE *file, BG_FILE_PROGRESS *progress);
void FileReset(BFILE *file);
const WCHAR *FileTarget(BFILE *file);
HRESULT FileExtFor(BFILE *file, IBackgroundCopyFile **out);
HRESULT FileEnumCreate(BJOB *job, IEnumBackgroundCopyFiles **out);
UINT64 FileRangeTotal(BFILE *file, UINT64 size);

/* ------------------------------------------------------------------ COM objects */


HRESULT QmEnter(void);
void QmLeave(void);
void QmObjectAdd(void);
void QmObjectRelease(void);
BOOL QmRunning(void);
void QmWake(void);
void QmThreadStart(void);
void QmThreadEnd(void);
HRESULT QmStartThread(LPTHREAD_START_ROUTINE routine, void *argument, HANDLE *out);

HRESULT ManagerCreate(REFIID iid, void **out);
HRESULT JobExtFor(BJOB *job, IBackgroundCopyJob **out);
HRESULT EnumCreate(const IID *iid, IUnknown **items, ULONG count, void **out);
HRESULT EnumGuidCreate(const IID *iid, const GUID *items, ULONG count, void **out);
HRESULT ErrorCreate(BJOB *job, IBackgroundCopyError **out);
HRESULT LegacyManagerCreate(REFIID iid, void **out);
HRESULT LegacyGroupFor(BJOB *job, IUnknown **out);
HRESULT LegacyJobFor(BJOB *job, IUnknown **out);

/* ------------------------------------------------------------------ messages, messages.c and error.c */

typedef struct QM_MESSAGE {
    DWORD id;
    const WCHAR *text;
} QM_MESSAGE;

extern const QM_MESSAGE g_messages[];
extern const DWORD g_message_count;
extern const QM_MESSAGE g_winhttp_messages[];
extern const DWORD g_winhttp_message_count;

HRESULT QmDescribe(HRESULT code, DWORD language, WCHAR **text);

/* ------------------------------------------------------------------ the scheduler, schedule.c */

void ScheduleStart(void);
void ScheduleStop(void);
void ScheduleInterrupt(BJOB *job);
void ScheduleNow(void);
void ScheduleWaitIdle(BJOB *job);
BOOL ScheduleIdle(void);
void RunRelease(RUN *run);
void RunFinish(RUN *run);
BOOL RunCurrent(RUN *run);

/* ------------------------------------------------------------------ transfers, download.c and upload.c */

/* a request of a job: what XFER_REQUEST points at lives here until TransferFree. */
typedef struct XREQ {
    XFER_REQUEST q;
    XFER_CREDENTIAL creds[MAX_CREDS];
    WCHAR *passwords[MAX_CREDS];
    WINHTTP_PROXY_INFO proxy;
    WCHAR *proxy_text;
    WCHAR *headers;
    const void *cert;
    HANDLE network_token;
    BOOL validate;
    HRESULT cert_error;
    DWORD status;
    BJOB *job;
    BFILE *file;
    void *sink;
} XREQ;

HINTERNET TransferSession(void);
void TransferCloseSession(void);
void TransferRequest(RUN *run, BFILE *file, const WCHAR *url, const WCHAR *extra_headers, XREQ *x);
HRESULT TransferRun(XREQ *x, XFER_RESULT *r);
void TransferFree(XREQ *x);
HRESULT TransferStatus(DWORD status);
BOOL TransferFatal(HRESULT hr, DWORD status);
HRESULT TransferCheckResponse(const XFER_RESULT *r);
void TransferKeepResponse(BFILE *file, const XFER_RESULT *r);
DWORD WINAPI DownloadWorker(void *argument);
DWORD WINAPI UploadWorker(void *argument);
HRESULT UploadGenerateReply(BJOB *job);
HRESULT UploadCommitReply(BJOB *job);
HRESULT UploadCancelSession(BJOB *job);

/* ------------------------------------------------------------------ notifications, notify.c */

void NotifySchedule(BJOB *job, UINT64 when);
void NotifyModification(BJOB *job);
void NotifyCancel(BJOB *job);
BOOL NotifyPending(BJOB *job);
BOOL NotifyEnabled(BJOB *job, ULONG flag);
HRESULT NotifySetInterface(BJOB *job, IUnknown *callback);
HRESULT NotifyGetInterface(BJOB *job, IUnknown **callback);
void NotifyRevoke(BJOB *job);
HRESULT NotifySetValidator(BJOB *job, IUnknown *validator);
HRESULT NotifyGetValidator(BJOB *job, IBackgroundCopyServerCertificateValidationCallback **out);
void NotifyStart(void);
void NotifyStop(void);
HRESULT NotifyBlanket(IUnknown *proxy, HANDLE token);

/* ------------------------------------------------------------------ persistence, store.c */

void StoreOpen(void);
void StoreClose(void);
void StoreSaveJob(BJOB *job);
void StoreDeleteJob(BJOB *job);
void StoreLoadJobs(void);

#endif
