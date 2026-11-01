/*
 * mstask.h: the task scheduler 1.0 interfaces of mstask.dll, which keep a task as a file below the tasks folder. A task
 * is the class CLSID_CTask, made by NewWorkItem of the scheduler CLSID_CTaskScheduler.
 */
#ifndef MIME_WIN32_MSTASK_H
#define MIME_WIN32_MSTASK_H

#include <mime/win32/objbase.h>
#include <mime/win32/shobjidl.h>

#define TASK_SUNDAY 0x1
#define TASK_MONDAY 0x2
#define TASK_TUESDAY 0x4
#define TASK_WEDNESDAY 0x8
#define TASK_THURSDAY 0x10
#define TASK_FRIDAY 0x20
#define TASK_SATURDAY 0x40

#define TASK_FIRST_WEEK 1
#define TASK_LAST_WEEK 5

#define TASK_TRIGGER_FLAG_HAS_END_DATE 0x1
#define TASK_TRIGGER_FLAG_KILL_AT_DURATION_END 0x2
#define TASK_TRIGGER_FLAG_DISABLED 0x4

#define TASK_TIME_TRIGGER_ONCE 0
#define TASK_TIME_TRIGGER_DAILY 1
#define TASK_TIME_TRIGGER_WEEKLY 2
#define TASK_TIME_TRIGGER_MONTHLYDATE 3
#define TASK_TIME_TRIGGER_MONTHLYDOW 4
#define TASK_EVENT_TRIGGER_ON_IDLE 5
#define TASK_EVENT_TRIGGER_AT_SYSTEMSTART 6
#define TASK_EVENT_TRIGGER_AT_LOGON 7

#define TASK_FLAG_INTERACTIVE 0x1
#define TASK_FLAG_DELETE_WHEN_DONE 0x2
#define TASK_FLAG_DISABLED 0x4
#define TASK_FLAG_START_ONLY_IF_IDLE 0x10
#define TASK_FLAG_KILL_ON_IDLE_END 0x20
#define TASK_FLAG_DONT_START_IF_ON_BATTERIES 0x40
#define TASK_FLAG_KILL_IF_GOING_ON_BATTERIES 0x80
#define TASK_FLAG_RUN_ONLY_IF_DOCKED 0x100
#define TASK_FLAG_HIDDEN 0x200
#define TASK_FLAG_RUN_IF_CONNECTED_TO_INTERNET 0x400
#define TASK_FLAG_RESTART_ON_IDLE_RESUME 0x800
#define TASK_FLAG_SYSTEM_REQUIRED 0x1000
#define TASK_FLAG_RUN_ONLY_IF_LOGGED_ON 0x2000

#define SCHED_S_TASK_READY ((HRESULT)0x00041300L)
#define SCHED_S_TASK_RUNNING ((HRESULT)0x00041301L)
#define SCHED_S_TASK_DISABLED ((HRESULT)0x00041302L)
#define SCHED_S_TASK_HAS_NOT_RUN ((HRESULT)0x00041303L)
#define SCHED_S_TASK_NO_MORE_RUNS ((HRESULT)0x00041304L)
#define SCHED_S_TASK_NOT_SCHEDULED ((HRESULT)0x00041305L)
#define SCHED_S_TASK_TERMINATED ((HRESULT)0x00041306L)
#define SCHED_S_TASK_NO_VALID_TRIGGERS ((HRESULT)0x00041307L)
#define SCHED_S_EVENT_TRIGGER ((HRESULT)0x00041308L)
#define SCHED_E_TRIGGER_NOT_FOUND ((HRESULT)0x80041309L)
#define SCHED_E_TASK_NOT_READY ((HRESULT)0x8004130AL)
#define SCHED_E_TASK_NOT_RUNNING ((HRESULT)0x8004130BL)
#define SCHED_E_SERVICE_NOT_INSTALLED ((HRESULT)0x8004130CL)
#define SCHED_E_CANNOT_OPEN_TASK ((HRESULT)0x8004130DL)
#define SCHED_E_INVALID_TASK ((HRESULT)0x8004130EL)
#define SCHED_E_ACCOUNT_INFORMATION_NOT_SET ((HRESULT)0x8004130FL)
#define SCHED_E_ACCOUNT_NAME_NOT_FOUND ((HRESULT)0x80041310L)
#define SCHED_E_ACCOUNT_DBASE_CORRUPT ((HRESULT)0x80041311L)
#define SCHED_E_NO_SECURITY_SERVICES ((HRESULT)0x80041312L)
#define SCHED_E_UNKNOWN_OBJECT_VERSION ((HRESULT)0x80041313L)
#define SCHED_E_UNSUPPORTED_ACCOUNT_OPTION ((HRESULT)0x80041314L)
#define SCHED_E_SERVICE_NOT_RUNNING ((HRESULT)0x80041315L)

MIME_DEFINE_GUID(CLSID_CTask, 0x148BD520, 0xA2AB, 0x11CE, 0xB1, 0x1F, 0x00, 0xAA, 0x00, 0x53, 0x05, 0x03);
MIME_DEFINE_GUID(CLSID_CTaskScheduler, 0x148BD52A, 0xA2AB, 0x11CE, 0xB1, 0x1F, 0x00, 0xAA, 0x00, 0x53, 0x05, 0x03);
MIME_DEFINE_GUID(IID_ITask, 0x148BD524, 0xA2AB, 0x11CE, 0xB1, 0x1F, 0x00, 0xAA, 0x00, 0x53, 0x05, 0x03);
MIME_DEFINE_GUID(IID_IEnumWorkItems, 0x148BD528, 0xA2AB, 0x11CE, 0xB1, 0x1F, 0x00, 0xAA, 0x00, 0x53, 0x05, 0x03);
MIME_DEFINE_GUID(IID_ITaskScheduler, 0x148BD527, 0xA2AB, 0x11CE, 0xB1, 0x1F, 0x00, 0xAA, 0x00, 0x53, 0x05, 0x03);
MIME_DEFINE_GUID(IID_ITaskTrigger, 0x148BD52B, 0xA2AB, 0x11CE, 0xB1, 0x1F, 0x00, 0xAA, 0x00, 0x53, 0x05, 0x03);
MIME_DEFINE_GUID(IID_IScheduledWorkItem, 0xA6B952F0, 0xA4B1, 0x11D0, 0x99, 0x7D, 0x00, 0xAA, 0x00, 0x68, 0x87, 0xEC);

typedef struct _TASK_TRIGGER {
    WORD cbTriggerSize;
    WORD Reserved1;
    WORD wBeginYear;
    WORD wBeginMonth;
    WORD wBeginDay;
    WORD wEndYear;
    WORD wEndMonth;
    WORD wEndDay;
    WORD wStartHour;
    WORD wStartMinute;
    DWORD MinutesDuration;
    DWORD MinutesInterval;
    DWORD rgFlags;
    DWORD TriggerType;
    union {
        struct {
            WORD DaysInterval;
        } Daily;
        struct {
            WORD WeeksInterval;
            WORD rgfDaysOfTheWeek;
        } Weekly;
        struct {
            DWORD rgfDays;
            WORD rgfMonths;
        } MonthlyDate;
        struct {
            WORD wWhichWeek;
            WORD rgfDaysOfTheWeek;
            WORD rgfMonths;
        } MonthlyDOW;
    } Type;
    WORD Reserved2;
    WORD wRandomMinutesInterval;
} TASK_TRIGGER, *PTASK_TRIGGER;
MIME_STATIC_ASSERT(sizeof(TASK_TRIGGER) == 48, "task trigger layout");

typedef struct ITaskTrigger ITaskTrigger;
typedef struct ITaskTriggerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITaskTrigger *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITaskTrigger *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITaskTrigger *This);
    HRESULT(STDMETHODCALLTYPE *SetTrigger)(ITaskTrigger *This, const TASK_TRIGGER *trigger);
    HRESULT(STDMETHODCALLTYPE *GetTrigger)(ITaskTrigger *This, TASK_TRIGGER *trigger);
    HRESULT(STDMETHODCALLTYPE *GetTriggerString)(ITaskTrigger *This, LPWSTR *text);
} ITaskTriggerVtbl;
struct ITaskTrigger {
    const ITaskTriggerVtbl *lpVtbl;
};

typedef struct ITask ITask, IScheduledWorkItem;
typedef struct ITaskVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITask *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITask *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITask *This);
    HRESULT(STDMETHODCALLTYPE *CreateTrigger)(ITask *This, WORD *index, ITaskTrigger **trigger);
    HRESULT(STDMETHODCALLTYPE *DeleteTrigger)(ITask *This, WORD index);
    HRESULT(STDMETHODCALLTYPE *GetTriggerCount)(ITask *This, WORD *count);
    HRESULT(STDMETHODCALLTYPE *GetTrigger)(ITask *This, WORD index, ITaskTrigger **trigger);
    HRESULT(STDMETHODCALLTYPE *GetTriggerString)(ITask *This, WORD index, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *GetRunTimes)(ITask *This, const SYSTEMTIME *begin, const SYSTEMTIME *end, WORD *count,
                                            SYSTEMTIME **times);
    HRESULT(STDMETHODCALLTYPE *GetNextRunTime)(ITask *This, SYSTEMTIME *next);
    HRESULT(STDMETHODCALLTYPE *SetIdleWait)(ITask *This, WORD idle_minutes, WORD deadline_minutes);
    HRESULT(STDMETHODCALLTYPE *GetIdleWait)(ITask *This, WORD *idle_minutes, WORD *deadline_minutes);
    HRESULT(STDMETHODCALLTYPE *Run)(ITask *This);
    HRESULT(STDMETHODCALLTYPE *Terminate)(ITask *This);
    HRESULT(STDMETHODCALLTYPE *EditWorkItem)(ITask *This, HWND parent, DWORD reserved);
    HRESULT(STDMETHODCALLTYPE *GetMostRecentRunTime)(ITask *This, SYSTEMTIME *last);
    HRESULT(STDMETHODCALLTYPE *GetStatus)(ITask *This, HRESULT *status);
    HRESULT(STDMETHODCALLTYPE *GetExitCode)(ITask *This, DWORD *code);
    HRESULT(STDMETHODCALLTYPE *SetComment)(ITask *This, LPCWSTR comment);
    HRESULT(STDMETHODCALLTYPE *GetComment)(ITask *This, LPWSTR *comment);
    HRESULT(STDMETHODCALLTYPE *SetCreator)(ITask *This, LPCWSTR creator);
    HRESULT(STDMETHODCALLTYPE *GetCreator)(ITask *This, LPWSTR *creator);
    HRESULT(STDMETHODCALLTYPE *SetWorkItemData)(ITask *This, WORD size, BYTE *data);
    HRESULT(STDMETHODCALLTYPE *GetWorkItemData)(ITask *This, WORD *size, BYTE **data);
    HRESULT(STDMETHODCALLTYPE *SetErrorRetryCount)(ITask *This, WORD count);
    HRESULT(STDMETHODCALLTYPE *GetErrorRetryCount)(ITask *This, WORD *count);
    HRESULT(STDMETHODCALLTYPE *SetErrorRetryInterval)(ITask *This, WORD minutes);
    HRESULT(STDMETHODCALLTYPE *GetErrorRetryInterval)(ITask *This, WORD *minutes);
    HRESULT(STDMETHODCALLTYPE *SetFlags)(ITask *This, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetFlags)(ITask *This, DWORD *flags);
    HRESULT(STDMETHODCALLTYPE *SetAccountInformation)(ITask *This, LPCWSTR account, LPCWSTR password);
    HRESULT(STDMETHODCALLTYPE *GetAccountInformation)(ITask *This, LPWSTR *account);
    HRESULT(STDMETHODCALLTYPE *SetApplicationName)(ITask *This, LPCWSTR name);
    HRESULT(STDMETHODCALLTYPE *GetApplicationName)(ITask *This, LPWSTR *name);
    HRESULT(STDMETHODCALLTYPE *SetParameters)(ITask *This, LPCWSTR parameters);
    HRESULT(STDMETHODCALLTYPE *GetParameters)(ITask *This, LPWSTR *parameters);
    HRESULT(STDMETHODCALLTYPE *SetWorkingDirectory)(ITask *This, LPCWSTR directory);
    HRESULT(STDMETHODCALLTYPE *GetWorkingDirectory)(ITask *This, LPWSTR *directory);
    HRESULT(STDMETHODCALLTYPE *SetPriority)(ITask *This, DWORD priority);
    HRESULT(STDMETHODCALLTYPE *GetPriority)(ITask *This, DWORD *priority);
    HRESULT(STDMETHODCALLTYPE *SetTaskFlags)(ITask *This, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetTaskFlags)(ITask *This, DWORD *flags);
    HRESULT(STDMETHODCALLTYPE *SetMaxRunTime)(ITask *This, DWORD milliseconds);
    HRESULT(STDMETHODCALLTYPE *GetMaxRunTime)(ITask *This, DWORD *milliseconds);
} ITaskVtbl;
struct ITask {
    const ITaskVtbl *lpVtbl;
};

typedef struct IEnumWorkItems IEnumWorkItems;
typedef struct IEnumWorkItemsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumWorkItems *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumWorkItems *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumWorkItems *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumWorkItems *This, ULONG count, LPWSTR **names, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumWorkItems *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumWorkItems *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumWorkItems *This, IEnumWorkItems **enumerator);
} IEnumWorkItemsVtbl;
struct IEnumWorkItems {
    const IEnumWorkItemsVtbl *lpVtbl;
};

typedef struct ITaskScheduler ITaskScheduler;
typedef struct ITaskSchedulerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITaskScheduler *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITaskScheduler *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITaskScheduler *This);
    HRESULT(STDMETHODCALLTYPE *SetTargetComputer)(ITaskScheduler *This, LPCWSTR computer);
    HRESULT(STDMETHODCALLTYPE *GetTargetComputer)(ITaskScheduler *This, LPWSTR *computer);
    HRESULT(STDMETHODCALLTYPE *Enum)(ITaskScheduler *This, IEnumWorkItems **enumerator);
    HRESULT(STDMETHODCALLTYPE *Activate)(ITaskScheduler *This, LPCWSTR name, REFIID riid, IUnknown **unknown);
    HRESULT(STDMETHODCALLTYPE *Delete)(ITaskScheduler *This, LPCWSTR name);
    HRESULT(STDMETHODCALLTYPE *NewWorkItem)(ITaskScheduler *This, LPCWSTR name, REFCLSID clsid, REFIID riid,
                                            IUnknown **unknown);
    HRESULT(STDMETHODCALLTYPE *AddWorkItem)(ITaskScheduler *This, LPCWSTR name, IScheduledWorkItem *item);
    HRESULT(STDMETHODCALLTYPE *IsOfType)(ITaskScheduler *This, LPCWSTR name, REFIID riid);
} ITaskSchedulerVtbl;
struct ITaskScheduler {
    const ITaskSchedulerVtbl *lpVtbl;
};

#endif
