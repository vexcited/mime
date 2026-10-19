/*
 * services.exe, the service control manager. see docs/services.md for what it does and where it follows windows'
 * services.exe of build 26300 and where it is a guess.
 *
 * the database is the registry below HKLM\SYSTEM\CurrentControlSet\Services, read at the start and kept in
 * records afterwards. one lock protects the records, the handles and the images; every wait releases it and
 * waits on the condition variable that any change of state wakes.
 */
#ifndef MIME_SERVICES_H
#define MIME_SERVICES_H

#include <mime/scm.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/winnls.h>

/* ------------------------------------------------------------------ what services.exe needs of advapi32 */

typedef struct _LUID_AND_ATTRIBUTES {
    LUID Luid;
    DWORD Attributes;
} LUID_AND_ATTRIBUTES;

typedef struct _TOKEN_PRIVILEGES {
    DWORD PrivilegeCount;
    LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES;

typedef struct _GENERIC_MAPPING {
    ACCESS_MASK GenericRead, GenericWrite, GenericExecute, GenericAll;
} GENERIC_MAPPING;

typedef struct _PRIVILEGE_SET {
    DWORD PrivilegeCount;
    DWORD Control;
    LUID_AND_ATTRIBUTES Privilege[1];
} PRIVILEGE_SET;

#define SECURITY_MAX_SID_SIZE_ 68
#define SECURITY_DESCRIPTOR_MIN_LENGTH_ 64
#define ERROR_INVALID_SECURITY_DESCR_ 1338L
typedef struct _SECURITY_QUALITY_OF_SERVICE_ {
    DWORD Length;
    DWORD ImpersonationLevel;
    BYTE ContextTrackingMode;
    BYTE EffectiveOnly;
} SECURITY_QUALITY_OF_SERVICE_;
#define TOKEN_ASSIGN_PRIMARY 0x0001
#define TOKEN_DUPLICATE 0x0002
#define TOKEN_IMPERSONATE 0x0004
#define TOKEN_QUERY 0x0008
#define TOKEN_ADJUST_PRIVILEGES 0x0020
#define TOKEN_ADJUST_DEFAULT 0x0080
#define TOKEN_ADJUST_SESSIONID 0x0100
#define TOKEN_ALL_ACCESS 0xF01FF
#define SE_PRIVILEGE_ENABLED 0x2
#define SE_PRIVILEGE_REMOVED 0x4
#define GENERIC_ALL_RIGHT 0x10000000
#define OWNER_SECURITY_INFORMATION 0x1
#define GROUP_SECURITY_INFORMATION 0x2
#define DACL_SECURITY_INFORMATION 0x4
#define SACL_SECURITY_INFORMATION 0x8
#define LABEL_SECURITY_INFORMATION 0x10
#define SDDL_REVISION_1 1
#define TokenUser 1
#define TokenPrimary 1
#define TokenImpersonation 2
#define TokenSessionId 12

typedef struct _TOKEN_USER_ {
    PSID Sid;
    DWORD Attributes;
} TOKEN_USER_;

WINBASEAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL as_self, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, DWORD klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI BOOL WINAPI SetTokenInformation(HANDLE token, DWORD klass, LPVOID info, DWORD length);
WINBASEAPI BOOL WINAPI AdjustTokenPrivileges(HANDLE token, BOOL disable_all, TOKEN_PRIVILEGES *state, DWORD length,
                                             TOKEN_PRIVILEGES *previous, PDWORD returned);
WINBASEAPI BOOL WINAPI DuplicateTokenEx(HANDLE token, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD level, DWORD type,
                                        PHANDLE out);
WINBASEAPI BOOL WINAPI LookupPrivilegeValueW(LPCWSTR system, LPCWSTR name, PLUID luid);
WINBASEAPI BOOL WINAPI ImpersonateNamedPipeClient(HANDLE pipe);
WINBASEAPI BOOL WINAPI RevertToSelf(void);
WINBASEAPI BOOL WINAPI AccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, GENERIC_MAPPING *mapping,
                                   PRIVILEGE_SET *privileges, LPDWORD privileges_length, LPDWORD granted,
                                   LPBOOL status);
WINBASEAPI void WINAPI MapGenericMask(PDWORD mask, GENERIC_MAPPING *mapping);
WINBASEAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                            PSECURITY_DESCRIPTOR *sd, PULONG size);
WINBASEAPI DWORD WINAPI GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR sd);
WINBASEAPI BOOL WINAPI IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR sd);
WINBASEAPI BOOL WINAPI GetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR sd, PSID *owner, LPBOOL defaulted);
WINBASEAPI BOOL WINAPI GetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR sd, PSID *group, LPBOOL defaulted);
WINBASEAPI BOOL WINAPI GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PVOID *dacl, LPBOOL defaulted);
WINBASEAPI BOOL WINAPI GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PVOID *sacl, LPBOOL defaulted);
WINBASEAPI BOOL WINAPI GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR sd, PWORD control, LPDWORD revision);
WINBASEAPI BOOL WINAPI MakeSelfRelativeSD(PSECURITY_DESCRIPTOR absolute, PSECURITY_DESCRIPTOR relative, LPDWORD length);
WINBASEAPI BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR sd, DWORD revision);
WINBASEAPI BOOL WINAPI SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR sd, PSID owner, BOOL defaulted);
WINBASEAPI BOOL WINAPI SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR sd, PSID group, BOOL defaulted);
WINBASEAPI BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, BOOL present, PVOID dacl, BOOL defaulted);
WINBASEAPI BOOL WINAPI SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR sd, BOOL present, PVOID sacl, BOOL defaulted);
WINBASEAPI BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *text);
WINBASEAPI BOOL WINAPI ConvertStringSidToSidW(LPCWSTR text, PSID *sid);
WINBASEAPI BOOL WINAPI LookupAccountSidW(LPCWSTR system, PSID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                         LPDWORD domain_size, PDWORD use);
WINBASEAPI BOOL WINAPI LookupAccountNameW(LPCWSTR system, LPCWSTR name, PSID sid, LPDWORD sid_size, LPWSTR domain,
                                          LPDWORD domain_size, PDWORD use);
WINBASEAPI BOOL WINAPI EqualSid(PSID a, PSID b);
WINBASEAPI DWORD WINAPI GetLengthSid(PSID sid);
WINBASEAPI BOOL WINAPI LogonUserW(LPCWSTR user, LPCWSTR domain, LPCWSTR password, DWORD type, DWORD provider,
                                  PHANDLE token);
WINBASEAPI BOOL WINAPI CreateProcessAsUserW(HANDLE token, LPCWSTR application, LPWSTR command_line,
                                            LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa,
                                            BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
                                            LPSTARTUPINFOW startup, LPPROCESS_INFORMATION info);
WINBASEAPI BOOL WINAPI InitiateSystemShutdownExW(LPWSTR machine, LPWSTR message, DWORD timeout, BOOL force,
                                                 BOOL reboot, DWORD reason);
WINBASEAPI BOOL WINAPI SetKernelObjectSecurity(HANDLE handle, DWORD information, PSECURITY_DESCRIPTOR sd);
WINBASEAPI BOOL WINAPI GetKernelObjectSecurity(HANDLE handle, DWORD information, PSECURITY_DESCRIPTOR sd, DWORD length,
                                               LPDWORD needed);
WINBASEAPI BOOL WINAPI AllocateLocallyUniqueId(PLUID luid);
WINBASEAPI BOOL WINAPI GetComputerNameW(LPWSTR name, LPDWORD size);
DWORD AccessServiceSid(const WCHAR *name, BYTE *sid);
WINBASEAPI NTSTATUS NTAPI NtCreateToken(PHANDLE token, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG type,
                                        PLUID authentication_id, PLARGE_INTEGER expiration, PVOID user, PVOID groups,
                                        PVOID privileges, PVOID owner, PVOID primary_group, PVOID default_dacl,
                                        PVOID source);

/* ------------------------------------------------------------------ records */

#define SVC_NAME_MAX 256
#define MAX_PATH_ 260
#define SVC_MAX_FAILURE_ACTIONS 16

/* ServiceFlags of a record. */
#define SVF_DELETE_PENDING 0x0001 /* DeleteService was called, the key goes when the last handle closes */
#define SVF_FAILURE_NON_CRASH 0x0020
#define SVF_DELAYED 0x0008        /* started after the others, DelayedAutostart */
#define SVF_STOP_SENT 0x0010      /* a stopping control was given to the service */
#define SVF_STOPPING_ALL 0x0100   /* the shutdown is stopping it */

typedef struct SERVICE SERVICE;
typedef struct IMAGE IMAGE;
typedef struct CLIENT CLIENT;
typedef struct NOTIFY NOTIFY;

struct SERVICE {
    SERVICE *Next;
    WCHAR Name[SVC_NAME_MAX + 1];
    WCHAR *DisplayName; /* the name when not set */
    WCHAR *Group;       /* NULL when it has none */
    WCHAR *DepServices; /* DependOnService, a multi sz */
    WCHAR *DepGroups;   /* DependOnGroup */
    DWORD Type, Start, ErrorControl, Tag;
    DWORD Flags;
    BOOL Loaded;
    /* the running state, as the service reports it. */
    SERVICE_STATUS Status;
    DWORD ServiceFlags; /* SERVICE_RUNS_IN_SYSTEM_PROCESS and the like */
    IMAGE *Image;
    UINT64 StatusId; /* the handle the service process reports the status with */
    ULONGLONG StateTick;
    DWORD StartError; /* the answer of the last start */
    DWORD OpenHandles;
    DWORD StartPending; /* a thread is starting it */
    /* failures, for the recovery actions. */
    DWORD FailureCount;
    ULONGLONG LastFailureTick;
    /* the access control list. */
    PSECURITY_DESCRIPTOR Sd;
    NOTIFY *Notify;
    ULONGLONG Stamp; /* when the state last changed, for notifications */
};

struct IMAGE {
    IMAGE *Next;
    WCHAR *Key;       /* the command line, which share process services match by */
    HANDLE Process;
    DWORD Pid;
    HANDLE Pipe;      /* the channel the dispatcher opened, NULL until it does */
    HANDLE PipeEvent; /* overlapped i/o of the channel */
    CRITICAL_SECTION ControlLock;
    HANDLE Wait;      /* the thread pool wait on the process */
    DWORD Services;   /* running or starting in it */
    BOOL Exited, Broken, Terminating;
    BOOL FirstSent;
    DWORD Type;
    BOOL Connected;
    DWORD Refs;
};

typedef struct SCHANDLE SCHANDLE;
struct SCHANDLE {
    SCHANDLE *Next;
    UINT64 Id;
    DWORD Kind;    /* SCH_* */
    DWORD Access;  /* granted, mapped */
    SERVICE *Service;
    DWORD Flags;
};
#define SCH_MANAGER 1
#define SCH_SERVICE 2

struct CLIENT {
    CLIENT *Next;
    DWORD Pid;
    HANDLE Process;
    HANDLE Wait;
    SCHANDLE *Handles;
    BOOL Locking; /* holds the lock of the database */
    BOOL Dead;    /* its process ended while a request was running */
    DWORD Refs;
    NOTIFY *Notifies;
    HANDLE NotifyPipe; /* where notifications go */
    HANDLE NotifyEvent;
};

struct NOTIFY {
    NOTIFY *Next;     /* of the service */
    NOTIFY *NextOfClient;
    CLIENT *Client;
    UINT64 Block;     /* chosen by the client */
    SERVICE *Service; /* NULL for the manager */
    DWORD Mask;
    DWORD Version;
    ULONGLONG Base;   /* the creation stamp or the state to compare with */
    DWORD LastState;
    BOOL Queued;      /* completed, waiting to be written */
    DWORD Status;
    DWORD Triggered;
    SERVICE_STATUS_PROCESS Process;
    WCHAR *Names;     /* multi sz for created and deleted */
    DWORD NamesChars;
    NOTIFY *NextQueued;
};

/* ------------------------------------------------------------------ globals */

extern CRITICAL_SECTION g_lock;
extern CONDITION_VARIABLE g_changed;
extern SERVICE *g_services;
extern BOOL g_shutdown;
extern BOOL g_database_ready;
extern DWORD g_control_timeout, g_handler_timeout, g_autostart_timeout;
extern BOOL g_autostart_running;
extern ULONGLONG g_stamp;

static inline void SvcCopyName(WCHAR *dst, const WCHAR *src, SIZE_T max)
{
    SIZE_T n = wcsnlen(src, max);
    memcpy(dst, src, n * sizeof(WCHAR));
    dst[n] = 0;
}

/* util.c */
void *SvcAlloc(SIZE_T size);
void *SvcAllocZero(SIZE_T size);
void SvcFree(void *p);
WCHAR *SvcDup(const WCHAR *s);
WCHAR *SvcDupN(const WCHAR *s, SIZE_T chars);
DWORD SvcMultiSzChars(const WCHAR *m);
BOOL SvcValidMultiSz(const WCHAR *m, DWORD chars);
UINT64 SvcNewId(void);
void SvcLog(const char *format, ...);
void SvcSleepChanged(DWORD ms);

/* db.c */
void DbLoadNames(void);
SERVICE *DbFind(const WCHAR *name);
SERVICE *DbFindByDisplay(const WCHAR *display);
BOOL DbLoadRecord(SERVICE *s);
DWORD DbCreate(const WCHAR *name, const WCHAR *display, DWORD type, DWORD start, DWORD error_control, const WCHAR *path,
               const WCHAR *group, DWORD *tag, const WCHAR *dependencies, DWORD dependencies_chars,
               const WCHAR *account, const WCHAR *password, SERVICE **out);
DWORD DbChange(SERVICE *s, DWORD type, DWORD start, DWORD error_control, const WCHAR *path, const WCHAR *group,
               DWORD *tag, const WCHAR *dependencies, DWORD dependencies_chars, const WCHAR *account,
               const WCHAR *password, const WCHAR *display);
DWORD DbCheckParameters(BOOL creating, const WCHAR *name, DWORD type, DWORD start, DWORD error_control,
                        const WCHAR *path, const WCHAR *group, const WCHAR *dependencies, DWORD dependencies_chars,
                        const WCHAR *account);
void DbRemove(SERVICE *s);
HKEY DbOpenKey(const WCHAR *name, REGSAM access, BOOL create);
WCHAR *DbReadString(HKEY key, const WCHAR *value, DWORD *chars);
BOOL DbReadDword(HKEY key, const WCHAR *value, DWORD *out);
DWORD DbWriteString(HKEY key, const WCHAR *value, const WCHAR *text, DWORD type);
DWORD DbWriteDword(HKEY key, const WCHAR *value, DWORD data);
const WCHAR *SvcDisplayName(const SERVICE *s);
BOOL DbDependsOn(SERVICE *s, SERVICE *other, DWORD depth);
void DbDependents(SERVICE *s, SERVICE ***list, DWORD *count, BOOL active_only);
DWORD DbReadFailureActions(SERVICE *s, SERVICE_FAILURE_ACTIONSW *actions, SC_ACTION *buffer, WCHAR **reboot,
                           WCHAR **command);
DWORD DbGroupIndex(const WCHAR *group);
DWORD DbServiceSids(SERVICE *s);
BOOL DbDriverRunning(SERVICE *s);

/* run.c */
DWORD RunStart(SERVICE *s, DWORD count, const WCHAR *const *arguments, DWORD reason);
DWORD RunControl(SERVICE *s, DWORD control, DWORD event_type, const void *event_data, DWORD event_size, DWORD *result);
DWORD RunSetStatus(UINT64 status_id, DWORD pid, const SERVICE_STATUS *status);
DWORD RunOpenChannel(HANDLE pipe, DWORD pid);
void RunChannelOpened(HANDLE pipe, DWORD pid, BOOL ok);
void RunStatusSnapshot(SERVICE *s, SERVICE_STATUS_PROCESS *out);
void RunStateChanged(SERVICE *s, DWORD old_state);
void RunStopAllForShutdown(void);
void RunProcessExited(IMAGE *image);
void RunScheduleRestart(SERVICE *s, DWORD delay);
BOOL RunStopSent(SERVICE *s);
BOOL RunImageExists(SERVICE *s);
DWORD RunWaitState(SERVICE *s, DWORD wanted, DWORD timeout);

/* notify.c */
void NotifyServiceChanged(SERVICE *s, DWORD old_state);
void NotifyServiceDeleting(SERVICE *s, BOOL gone);
void NotifyCreatedDeleted(const WCHAR *name, BOOL created);
DWORD NotifyRegister(CLIENT *client, SCHANDLE *handle, UINT64 block, DWORD mask, DWORD version, DWORD *status_out);
void NotifyClientGone(CLIENT *client);
DWORD NotifyAttach(CLIENT *client, HANDLE pipe);
void NotifyFlush(CLIENT *client);

/* access.c */
void AccessInit(void);
PSECURITY_DESCRIPTOR AccessDefaultServiceSd(void);
extern PSECURITY_DESCRIPTOR g_manager_sd;
extern GENERIC_MAPPING g_manager_mapping, g_service_mapping;
DWORD AccessCheckClient(HANDLE pipe, PSECURITY_DESCRIPTOR sd, DWORD desired, const GENERIC_MAPPING *mapping,
                        DWORD *granted);
DWORD AccessClientName(HANDLE pipe, WCHAR *name, DWORD chars);
BOOL AccessClientIsAdmin(HANDLE pipe);
DWORD AccessReadSd(SERVICE *s);
DWORD AccessWriteSd(SERVICE *s);
DWORD AccessQuerySd(PSECURITY_DESCRIPTOR sd, DWORD information, void *out, DWORD size, DWORD *needed);
DWORD AccessSetSd(PSECURITY_DESCRIPTOR *sd, DWORD information, PSECURITY_DESCRIPTOR modification);
HANDLE AccessServiceToken(SERVICE *s, DWORD *error);
void AccessEnablePrivilege(const WCHAR *name);

/* boot.c */
void BootStart(void);
void ShutdownInit(void);
void TimerAdd(DWORD delay, void (*callback)(void *), void *context);
void TimerInit(void);

/* main.c */
void MainWake(void);
void MainCollect(void);
void entry(void);

/* the failure actions as the service keeps them in the registry. */
typedef struct FAILURE_BLOB {
    DWORD ResetPeriod;
    DWORD RebootMessage; /* offsets from the start of the value */
    DWORD Command;
    DWORD ActionCount;
    DWORD Actions;
} FAILURE_BLOB;

#endif
