/*
 * mime kernel internal interface.
 */
#ifndef MIME_NTOS_H
#define MIME_NTOS_H

#define MIME_KERNEL 1
#include <mime/abi.h>
#include <mime/ddk.h>
#include <mime/ntapi.h>
#include <mime/mitigation.h>

#include "../rtl/rtl.h"

#define KI_TAG(a, b, c, d) ((ULONG)(a) | (ULONG)(b) << 8 | (ULONG)(c) << 16 | (ULONG)(d) << 24)

#ifdef __x86_64__
#include "arch/x64/arch.h"
#else
#include "arch/arm64/arch.h"
#endif

[[gnu::returns_twice]] int KiSetJump(KJUMP_BUFFER *buffer);
DECLSPEC_NORETURN void KiLongJump(KJUMP_BUFFER *buffer, int value);
void KiThreadStartup(void);
DECLSPEC_NORETURN void KiEnterUserMode(PKTRAP_FRAME frame);
NTSTATUS KiCallUserMode(ULONG64 *callback_sp_slot, PKTRAP_FRAME frame);
DECLSPEC_NORETURN void KiCallbackReturnInternal(ULONG64 callback_sp, NTSTATUS status);
ULONG64 KiCallService(PVOID routine, ULONG64 *args, ULONG stack_args);

/* ------------------------------------------------------------------ hal */

extern MimeBootInfo *HalBootInfo;
extern MimeSharedPage *HalSharedPage;

ULONG64 HalHypercall(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 a6,
                     PULONG64 value);
/*
 * a call to the host that can take milliseconds, as flushing a file does, made with the kernel lock let
 * go so that the other processors go on meanwhile. the caller holds no spin lock, is below
 * DISPATCH_LEVEL and relies on nothing that only the kernel lock guards until the call is back.
 */
ULONG64 HalHypercallUnlocked(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 a6,
                             PULONG64 value);
void HalInitialize(MimeBootInfo *boot);
void HalLog(const char *text, SIZE_T length);
DECLSPEC_NORETURN void HalPanic(const char *text);
ULONG64 HalPhysicalOf(const void *va);
void *HalPhysmap(ULONG64 pa);
ULONG64 HalReadCounter(void);
void HalArmTimer(ULONG64 deadline_counter);
/* the system files mapped by the host, see MimeRomHeader. paths are relative to C:. */
const MimeRomEntry *HalRomFind(PCWSTR path, ULONG chars);
const MimeRomEntry *HalRomEntry(ULONG index); /* NULL past the last entry */
ULONG64 HalRomPhysical(const MimeRomEntry *entry);
BOOLEAN HalIsRomPhysical(ULONG64 pa);
ULONG HalAcknowledgeInterrupt(void); /* interrupt id, 1020 and above when none is pending */
void HalEndOfInterrupt(ULONG intid);
void HalInitializeArch(void);
void HalInitializeProcessor(void);
void HalSendIpi(ULONG processor);
void HalSendIpiMask(ULONG64 mask);
#ifdef __x86_64__
void HalSendFlushIpi(ULONG64 mask);
#endif
void HalDisarmTimer(void);
#define HAL_IPI_INTID 1 /* the sgi asking a processor to reschedule */
ULONG64 HalCounterToHns(ULONG64 counter_delta);
ULONG64 HalHnsToCounter(ULONG64 hns);

void KiLogPrintf(const char *fmt, ...);
void KiBootStamp(const char *what);
void KiTimelineFault(ULONG64 started, ULONG kind, ULONG address_page);
void KiTraceUserStack(ULONG64 pc, ULONG64 sp);
BOOLEAN KiDescribeUserAddress(ULONG_PTR va, char *out, ULONG size);
void KiFormatUserStack(PEPROCESS process, ULONG64 sp, char *out, ULONG size);
#define KdPrint(args) KiLogPrintf args
#define MIME_TRACE(...) KiLogPrintf(__VA_ARGS__)

#define ASSERT(e)                                                                                                 \
    do {                                                                                                          \
        if (!(e))                                                                                                 \
            KiAssertFailed(#e, __FILE__, __LINE__);                                                               \
    } while (0)
DECLSPEC_NORETURN void KiAssertFailed(const char *expr, const char *file, int line);

/* ------------------------------------------------------------------ ke */

typedef enum _KTHREAD_STATE {
    Initialized,
    Ready,
    Running,
    Standby,
    Terminated,
    Waiting,
    Transition,
    DeferredReady,
} KTHREAD_STATE;

typedef struct _KWAIT_BLOCK {
    LIST_ENTRY WaitListEntry;
    struct _KTHREAD *Thread;
    PVOID Object;
    struct _KWAIT_BLOCK *NextWaitBlock;
    USHORT WaitKey;
    UCHAR WaitType;
    UCHAR BlockState;
    ULONG Pad;
} KWAIT_BLOCK, *PKWAIT_BLOCK;

#define THREAD_WAIT_OBJECTS 3
#define MAXIMUM_WAIT_OBJECTS 64
#define TIMER_WAIT_BLOCK 3
#define LOW_PRIORITY 0
#define LOW_REALTIME_PRIORITY 16
#define HIGH_PRIORITY 31
#define MAXIMUM_PRIORITY 32

typedef struct _KTHREAD {
    DISPATCHER_HEADER Header;
    ULONG64 KernelSp;
    PVOID InitialStack;
    PVOID StackLimit;
    PVOID StackAllocation;
    SIZE_T StackAllocationSize;
    PKTRAP_FRAME TrapFrame;
    struct _KPROCESS *Process;
    struct _KPROCESS *ApcStateProcess; /* attached process, or Process */
    PTEB Teb;
    PVOID Teb32; /* the 32-bit teb of a wow64 thread, its fs base */
    LIST_ENTRY ThreadListEntry;
    LIST_ENTRY ReadyListEntry;
    volatile UCHAR State;
    KPROCESSOR_MODE PreviousMode;
    KPROCESSOR_MODE WaitMode;
    BOOLEAN Alertable;
    BOOLEAN Alerted[2];
    volatile BOOLEAN UserApcPending;
    volatile BOOLEAN Terminating;
    SCHAR Priority;
    SCHAR BasePriority;
    SCHAR Saturation;      /* the sign of a base priority asked as a limit, -1 idle, 1 time critical */
    BOOLEAN DisableBoost;  /* recorded, nothing boosts */
    UCHAR IdealProcessor;  /* recorded, nothing honours it */
    UCHAR Pad2;
    ULONG64 Affinity;      /* the processors it may run on, recorded and not enforced */
    UCHAR WaitReason;
    BOOLEAN WaitIrqlRaised;
    UCHAR Processor; /* the processor it runs or last ran on */
    BOOLEAN IdleThread;
    LONG Quantum;
    LONG_PTR WaitStatus;
    PKWAIT_BLOCK WaitBlockList;
    KWAIT_BLOCK WaitBlock[THREAD_WAIT_OBJECTS + 1];
    KTIMER Timer;
    LIST_ENTRY MutantListHead;
    LIST_ENTRY ApcListHead[2]; /* kernel, user */
    volatile BOOLEAN KernelApcPending;
    SHORT KernelApcDisable;
    SHORT SpecialApcDisable;
    ULONG64 CallbackSp;
    ULONG CallbackDepth;
    ULONG CallbackResultLength;
    PVOID CallbackResult;
    PVOID Win32Thread;
    KJUMP_BUFFER *FaultRecovery;
    NTSTATUS FaultStatus; /* what an unresolved fault is, when it is not an access violation, see KiApplyFaultStatus */
    ULONG64 ContextSwitches;
    ULONG64 BlockedTicks; /* counter ticks spent switched out, to tell a service's work from its waiting */
    ULONG64 KernelTime;
    ULONG64 UserTime;
    ULONG SuspendCount;
    BOOLEAN SuspendApcActive;
    BOOLEAN Suspended;
    PVOID UserSuspendGate;
    KEVENT SuspendEvent;
    KSEMAPHORE SuspendSemaphore;
    KAPC SuspendApc;
    KEVENT AlertByIdEvent; /* NtAlertThreadByThreadId, sticky until the next NtWaitForAlertByThreadId */
    PVOID XSaveArea; /* x64: the extended state saved when switched out, NULL for system threads */
} KTHREAD;

typedef struct _KPROCESS {
    DISPATCHER_HEADER Header;
    ULONG64 DirectoryTableBase; /* the root table physical address, with the asid in bits 63:48 on arm64 */
    ULONG64 *PageDirectory; /* physmap address of the root table */
    USHORT Asid;
    SCHAR BasePriority;
    UCHAR Pad;
    ULONG64 Affinity;
    LIST_ENTRY ThreadListHead;
    volatile LONG ActiveThreads;
} KPROCESS;

#define MAXIMUM_PROCESSORS 64

/* what an idle processor does between work: runs a thread or leaves, spins on its flags, or is halted. */
#define KI_IDLE_RUNNING 0
#define KI_IDLE_POLLING 1
#define KI_IDLE_HALTED 2

typedef struct _KPRCB {
    ULONG Number;
    ULONG Pad;
    struct _KTHREAD *CurrentThread;
    struct _KTHREAD *NextThread;
    struct _KTHREAD *IdleThread;
    ULONG64 TickTime; /* interrupt time of the last clock tick charged on this processor */
    volatile BOOLEAN DispatchPending;
    volatile BOOLEAN DpcPending;
    volatile UCHAR IdleState; /* KI_IDLE_*: whether an idle processor looks for work by itself or has to be interrupted */
    BOOLEAN InDpc;
    BOOLEAN Pad2;
    SINGLE_LIST_ENTRY DpcHead;
    PSINGLE_LIST_ENTRY *DpcTail;
    ULONG64 InterruptCount;
    ULONG64 ContextSwitches;
    ULONG64 DpcCount;
    ULONG64 IdleTicks;
    ULONG64 KernelTime, UserTime; /* charged with the threads' at each clock tick, in 100ns */
    ULONG64 InterruptTime, DpcTime; /* spent in interrupt handlers and dpcs, part of KernelTime */
    ULONG64 IpiSent, IpiReceived;
    ULONG64 PendingSince; /* interrupt time an idle processor was asked to dispatch, 0 when not */
    ULONG64 TimerArmed;   /* counter value the processor's timer is set to, 0 when it is not (x64: it fires once) */
} KPRCB, *PKPRCB;

#ifdef __x86_64__
/* the processor's descriptor tables and the system call stack switch, then the fields of the kernel. */
typedef struct _KPCR {
    ULONG64 *GdtBase;
    PUCHAR TssBase;
    ULONG64 UserRsp; /* scratch of the system call entry */
    struct _KPCR *Self;
    PKPRCB CurrentPrcb;
    ULONG64 KernelRsp; /* top of the trap frame of the thread in user mode */
    PVOID IdtBase;
    ULONG64 Pad0[3];
    volatile KIRQL Irql;
    volatile UCHAR UserMode; /* set while the processor runs user mode, see entry.S */
    volatile UCHAR IdleSleeping; /* halted without the kernel lock, see KiIdleWait */
    volatile UCHAR FlushPending; /* KPCR_FLUSH_* the processor owes before using translations again */
    ULONG ApicId;
    ULONG64 SavedInterruptState; /* interrupt flag before irql rose above DISPATCH_LEVEL */
    ULONG64 Gdt[12];
    UCHAR Tss[0x68];
    UCHAR Pad1[0x180 - 0x60 - 12 * 8 - 0x68];
    KPRCB Prcb;
} KPCR, *PKPCR;
MIME_STATIC_ASSERT(offsetof(KPCR, Self) == KPCR_SELF, "pcr self");
MIME_STATIC_ASSERT(offsetof(KPCR, KernelRsp) == KPCR_KERNEL_RSP, "pcr kernel rsp");
MIME_STATIC_ASSERT(offsetof(KPCR, Irql) == KPCR_IRQL, "pcr irql");
MIME_STATIC_ASSERT(offsetof(KPCR, FlushPending) == KPCR_FLUSH_PENDING, "pcr flush");
MIME_STATIC_ASSERT(offsetof(KPCR, Prcb) == KPCR_PRCB, "pcr prcb");
MIME_STATIC_ASSERT(offsetof(KPCR, Prcb.CurrentThread) == KPCR_CURRENT_THREAD, "pcr thread");
#else
typedef struct _KPCR {
    struct _KPCR *Self;
    PKPRCB CurrentPrcb;
    volatile KIRQL Irql;
    volatile UCHAR UserMode; /* set while the processor runs user mode, see vectors.S */
    UCHAR Pad[6];
    ULONG64 SavedInterruptState; /* interrupt mask before irql rose above DISPATCH_LEVEL */
    volatile UCHAR *Redistributor;
    KPRCB Prcb;
} KPCR, *PKPCR;
#endif

MIME_STATIC_ASSERT(offsetof(KPCR, Prcb.Number) == KPCR_NUMBER, "pcr number");
MIME_STATIC_ASSERT(offsetof(KPCR, UserMode) == KPCR_USER_MODE, "pcr user mode");

extern KPCR KiBootPcr;
extern PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS];
extern ULONG KeNumberProcessors;

/*
 * the ready threads of every processor. a thread runs on any processor;
 * the kernel lock makes the queues and every other kernel structure safe.
 */
extern LIST_ENTRY KiReadyListHead[MAXIMUM_PRIORITY];
extern ULONG KiReadySummary;

FORCEINLINE PKPRCB KeGetCurrentPrcb(void)
{
    return KeGetPcr()->CurrentPrcb;
}

FORCEINLINE PKTHREAD KiCurrentThread(void)
{
    return KeGetPcr()->CurrentPrcb->CurrentThread;
}

/*
 * the kernel lock: a processor holds it whenever it runs kernel code with
 * interrupts enabled. entering the kernel from user mode takes it and
 * returning to user mode releases it (vectors.S); the idle loop drops it
 * only around wfi. user mode runs on every processor at once, while kernel
 * code keeps the single processor rules: masking interrupts excludes every
 * other kernel path.
 */
extern volatile ULONG KiKernelLock[8]; /* ticket lock, owning processor, sleepers in the host, what holds it, when it was taken, ticks held */
extern ULONG64 KiLongHoldTicks;
void KiNoteLongHold(ULONG64 held);
extern ULONG64 HalHypercallCalls[64], HalHypercallTicks[64], HalHypercallLongest[64], HalHypercallLockedTicks[64];
#define KI_ASSERT_KERNEL_LOCK()                                                                                   \
    do {                                                                                                          \
        if (KiKernelLock[1] != KeGetCurrentPrcb()->Number)                                                        \
            KeBugCheckEx(0xDEAD10C, KiKernelLock[1], KeGetCurrentPrcb()->Number, __LINE__, 0);                    \
    } while (0)
extern ULONG64 KiKernelLockPhys;
extern ULONG64 KiLockStats[MAXIMUM_PROCESSORS][8];
void KiAcquireKernelLock(void);
void KiReleaseKernelLock(void);

FORCEINLINE ULONG64 KiAcquireDispatcherLock(void)
{
    return KiDisableInterrupts();
}

FORCEINLINE void KiReleaseDispatcherLock(ULONG64 state)
{
    KiRestoreInterrupts(state);
}

/* kernel options from MIME_KERNEL_ARGS that turn the lock free paths off, to tell what a failure depends on. */
#define KI_OPT_NO_FAST_FAULT 0x1
#define KI_OPT_NO_SECTION_FAULT 0x2
#define KI_OPT_NO_IDLE_SPIN 0x4
#define KI_OPT_EAGER_TIMER 0x8
extern ULONG KiOptions;
extern UCHAR KiNoLockFlush;

void KiInitializeKernel(MimeBootInfo *boot);
void KiInitializeProcessor(PKPCR pcr);
void KiStartProcessors(void);
void KiSecondaryStartup(void);
void KiRequestDispatch(ULONG processor);
void KiInitializeScheduler(void);
void KiInitializeThread(PKTHREAD thread, PKPROCESS process, PVOID stack_top, PVOID stack_limit,
                        PKSTART_ROUTINE routine, PVOID context);
void KiReadyThread(PKTHREAD thread);
ULONG64 KiAllProcessors(void);
ULONG64 KiProcessAffinity(PKPROCESS process);
LONG KiQueryBasePriorityThread(PKTHREAD thread);
void KiSetBasePriorityThread(PKTHREAD thread, LONG increment);
void KiSetPriorityThread(PKTHREAD thread, int priority);
void KiSetBasePriorityProcess(PKPROCESS process, int base);
void KiInsertReadyThread(PKTHREAD thread);
void KiInterruptThread(PKTHREAD thread);
void KiStartThread(PKTHREAD thread);
void KiSwapThread(void);
void KiDispatchPendingWork(void);
void KiDeliverDpcs(void);
void KiChargeServiceTime(PKPRCB prcb, ULONG64 start_counter, BOOLEAN dpc);
void KiCheckPreemption(void);
NTSTATUS KiWaitObjects(ULONG count, PVOID objects[], WAIT_TYPE type, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                       BOOLEAN alertable, PLARGE_INTEGER timeout, PKWAIT_BLOCK blocks);
void KiSignalWaiters(PDISPATCHER_HEADER object, KPRIORITY increment);
void KiUnwaitThread(PKTHREAD thread, LONG_PTR status);
void KiInitializeTimers(void);
void KiClockInterrupt(PKTRAP_FRAME frame);
ULONG64 KiQueryInterruptTime(void);
ULONG64 KiQpcFrequency(void);
void KiProgramNextTimer(void);
DECLSPEC_NORETURN void KiTerminateCurrentThread(NTSTATUS status);
void KiInitializeProcess(PKPROCESS process, ULONG64 ttbr0, ULONG64 *pgd, USHORT asid);
void KiAttachProcess(PKPROCESS process);

/* arch/<processor>/context.c: the processor specific half of threads, contexts and user mode entry. */
void KiSwitchAddressSpace(PKPROCESS process);
void KiSwapContext(PKTHREAD old, PKTHREAD next);
void KiIdleWait(void); /* interrupts masked and the kernel lock held, both again on return */
void KiInitializeSwitchFrame(PKTHREAD thread, PKSTART_ROUTINE routine, PVOID context);
void KiInitializeUserFrame(PKTRAP_FRAME frame, ULONG64 pc, ULONG64 sp, ULONG64 arg0, ULONG64 arg1, ULONG64 teb);
void KiInitializeUserContext(PCONTEXT context, ULONG64 pc, ULONG64 sp, ULONG64 arg0, ULONG64 arg1, ULONG64 teb);
BOOLEAN KiSetupUserApc(PKTRAP_FRAME frame, PVOID dispatcher, PKNORMAL_ROUTINE routine, PVOID context, PVOID arg1,
                       PVOID arg2);
BOOLEAN KiSetupUserException(PKTRAP_FRAME frame, PVOID dispatcher, PEXCEPTION_RECORD record);
BOOLEAN KiSetupUserCallback(PKTRAP_FRAME frame, const KTRAP_FRAME *trap, PVOID dispatcher, ULONG api,
                            const void *input, ULONG input_length);
void KiDumpTrapFrame(const char *what, const KTRAP_FRAME *frame);
void KiFlushInstructionCache(void);
void KiInitializeProcessorArch(void);
void KiDeliverApcs(KPROCESSOR_MODE mode, PKTRAP_FRAME frame);
void PspCheckForSuspendApc(PKTHREAD thread);
NTSTATUS PspWaitForSuspension(PETHREAD thread);
BOOLEAN KiInsertQueueApc(PKAPC apc, KPRIORITY increment);
void KeInitializeApc(PKAPC apc, PKTHREAD thread, KAPC_ENVIRONMENT env, PKKERNEL_ROUTINE kernel_routine,
                     PKRUNDOWN_ROUTINE rundown, PKNORMAL_ROUTINE normal, KPROCESSOR_MODE mode, PVOID context);
BOOLEAN KeInsertQueueApc(PKAPC apc, PVOID arg1, PVOID arg2, KPRIORITY increment);
void KiExitToUserMode(PKTRAP_FRAME frame);
void KiSystemService(PKTRAP_FRAME frame);
NTSTATUS KiUserCallback(ULONG api, PVOID input, ULONG input_length, PVOID *output, PULONG output_length);
NTSTATUS KiCallbackReturn(PVOID result, ULONG result_length, NTSTATUS status);
void KiContextToTrapFrame(const CONTEXT *context, PKTRAP_FRAME frame, ULONG flags, KPROCESSOR_MODE mode);
void KiTrapFrameToContext(const KTRAP_FRAME *frame, CONTEXT *context, ULONG flags);
BOOLEAN KiDispatchUserException(PEXCEPTION_RECORD record, PKTRAP_FRAME frame);
NTSTATUS KeRegisterSystemServices(ULONG table, PKSERVICE_DESCRIPTOR services, ULONG count);
void KiRegisterService(ULONG number, PVOID routine, UCHAR stack_args);
NTSTATUS PsWin32ProcessCallout(PEPROCESS process, BOOLEAN initialize);
NTSTATUS PsWin32ThreadCallout(PETHREAD thread, BOOLEAN initialize);
ULONG PsWin32ShutdownProcess(PEPROCESS process, const MX_SHUTDOWN_CONTEXT *context);
/* ps/shutdown.c */
void PsShutdownInitialize(void);
DECLSPEC_NORETURN void PsShutdownSystem(ULONG action);

/* arch/<processor>/cpu.c, the identification of the boot processor. */
void KiReadProcessorIds(void);
BOOLEAN KiQueryHypervisorLeaves(ULONG leaves[][4], ULONG count);
void KiIdentifyToHypervisor(void);
void KiSetProcessorFeatures(BOOLEAN *features);
PCSTR KiProcessorVendor(void);
USHORT KiProcessorModel(void);
USHORT KiProcessorLevel(void);
USHORT KiProcessorRevision(void);
void KiFormatProcessorIdentifier(PWCHAR text, ULONG chars); /* "ARMv8 (64-bit) Family 8 Model..." */
typedef void (*KI_SET_REGISTRY_VALUE)(PCWSTR key, PCWSTR name, ULONG type, const void *data, ULONG length);
void KiSetProcessorRegistryValues(PCWSTR key, KI_SET_REGISTRY_VALUE set); /* the processor's own CentralProcessor values */
UCHAR KiProcessorSmbiosType(void);
PCSTR KiProcessorSmbiosManufacturer(void);
ULONG64 KiProcessorSmbiosId(void);

/* user memory access with recovery from faults. */
#define KI_TRY_USER(buf) (KiCurrentThread()->FaultRecovery = &(buf), KiSetJump(&(buf)) == 0)
#define KI_END_TRY_USER() (KiCurrentThread()->FaultRecovery = NULL)
NTSTATUS KiCopyFromUser(void *dst, const void *user_src, SIZE_T length);
/* what a copy that took a fault returns: the status of the fault, an access violation when it was just that. */
NTSTATUS KiFaultResult(void);
void KiApplyFaultStatus(PEXCEPTION_RECORD record);
NTSTATUS KiCopyToUser(void *user_dst, const void *src, SIZE_T length);
BOOLEAN KiIsUserRange(const void *address, SIZE_T length);
NTSTATUS KiCaptureUnicodeString(PUNICODE_STRING kernel_copy, PCUNICODE_STRING user_string);
void KiFreeCapturedString(PUNICODE_STRING s);

typedef enum _KI_CLOCK {
    KI_CLOCK_TICK_HNS = 156250, /* 15.625 ms, the windows default */
} KI_CLOCK;

/* ------------------------------------------------------------------ mm */

#define MM_PFN_FREE 0x1
#define MM_PFN_ALLOCATED 0x2
#define MM_PFN_PAGE_TABLE 0x4
#define MM_PFN_POOL 0x8
#define MM_PFN_RESERVED 0x10
#define MM_PFN_DISCARDED 0x20 /* free block head already handed back to the host */

typedef struct _MMPFN {
    LIST_ENTRY ListEntry;
    ULONG Flags;
    UCHAR Order;
    UCHAR Pad[3];
    volatile LONG RefCount;
    ULONG ShareCount;
} MMPFN, *PMMPFN;

typedef enum _MI_VAD_TYPE {
    MiVadPrivate,
    MiVadMapped,
    MiVadImage,
    MiVadShared, /* kernel provided shared memory, e.g. the desktop heap */
} MI_VAD_TYPE;

struct _SECTION;

/* MMVAD.Flags */
#define MM_VAD_PLACEHOLDER 0x1 /* a reservation that holds nothing, to be replaced by an allocation or a view */
#define MM_VAD_REPLACED 0x2    /* made by replacing a placeholder, so it can become one again */
#define MM_VAD_WRITE_WATCH 0x4 /* the pages written since they were last reset are tracked */
#define MM_VAD_RELOCATED 0x8        /* a view of an image away from its base: its pages are relocated as they are first touched */
#define MM_VAD_RELOCATED_SHARED 0x10 /* the relocated pages of the view are the section's, shared by the views at the same base */

typedef struct _MMVAD {
    ULONG_PTR StartVa;
    ULONG_PTR EndVa; /* exclusive */
    ULONG Type;
    ULONG InitialProtect;
    struct _SECTION *Section;
    ULONG64 SectionOffset;
    ULONG Flags;
    LONG64 RelocDelta; /* MM_VAD_RELOCATED: how far the view is from the preferred base */
} MMVAD, *PMMVAD;

typedef struct _MMVAD_SPAN {
    ULONG_PTR Start;
    ULONG_PTR End;
} MMVAD_SPAN;

/*
 * the vads of a process sorted by address: two parallel arrays, so that a
 * lookup is a binary search and looking for free space scans a dense array
 * of ranges. inserting moves the tail, which is cheap next to the hundreds
 * or thousands of vads a browser makes.
 */
typedef struct _MMADDRESS_SPACE {
    MMVAD_SPAN *Spans;
    PMMVAD *Vads;
    ULONG VadCount;
    ULONG VadCapacity;
    PMMVAD Hint;
    FAST_MUTEX Lock;
    ULONG64 *Root; /* physmap pointer to the l0 table */
    ULONG64 RootPhys;
    USHORT Asid;
    SIZE_T CommittedPages;       /* private pages backed by memory, the working set */
    SIZE_T PeakCommittedPages;
    SIZE_T CommitCharge;         /* private pages committed, backed or not */
    SIZE_T PeakCommitCharge;
    SIZE_T PeakVirtualSize;
    SIZE_T PageFaults;
    ULONG_PTR AllocationLimit;
    ULONG_PTR LastFaultEnd[8]; /* ends of the last demand zero windows, to spot sequential touching by several threads */
    ULONG NextFaultStream;
    /* faults that run without the kernel lock count in; the holder of Lock sets the writer bit and waits for them to leave. */
    volatile ULONG FaultGate;
} MMADDRESS_SPACE, *PMMADDRESS_SPACE;

#define MI_GATE_WRITER 0x80000000u
#define MI_GATE_READER 1u

/*
 * the lock of an address space. every change to its vads and translation tables happens inside it, and
 * so does every fault that is not the plain demand zero one, which MmFastFault resolves without the
 * kernel lock but only while the writer bit of the gate is clear.
 */
FORCEINLINE void MiLockAddressSpace(PMMADDRESS_SPACE as)
{
    ExAcquireFastMutex(&as->Lock);
    __atomic_fetch_or(&as->FaultGate, MI_GATE_WRITER, __ATOMIC_SEQ_CST);
    while (__atomic_load_n(&as->FaultGate, __ATOMIC_ACQUIRE) & ~MI_GATE_WRITER)
        KiPause();
}

FORCEINLINE void MiUnlockAddressSpace(PMMADDRESS_SPACE as)
{
    __atomic_fetch_and(&as->FaultGate, ~MI_GATE_WRITER, __ATOMIC_RELEASE);
    ExReleaseFastMutex(&as->Lock);
}

BOOLEAN MmFastFault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute);

/* faults of user mode by how they were resolved, for the thread dump. */
typedef struct DECLSPEC_ALIGN(64) _MM_FAULT_STATS {
    ULONG64 Fast;     /* demand zero, without the kernel lock */
    ULONG64 Gated;    /* turned away because a change of the address space was under way */
    ULONG64 Rejected; /* not a plain demand zero fault */
    ULONG64 Slow;     /* resolved or refused by MmAccessFault */
} MM_FAULT_STATS;
extern MM_FAULT_STATS MmFaultStats[MAXIMUM_PROCESSORS];

typedef struct _MI_IMAGE_INFO {
    ULONG_PTR ImageBase; /* preferred */
    ULONG SizeOfImage;
    ULONG EntryPoint;
    ULONG SizeOfHeaders;
    USHORT Machine;
    USHORT Subsystem;
    USHORT SubsystemMajor;
    USHORT SubsystemMinor;
    USHORT DllCharacteristics;
    USHORT Characteristics;
    ULONG64 StackReserve;
    ULONG64 StackCommit;
    BOOLEAN Relocatable;
} MI_IMAGE_INFO;


#define MI_SECTION_PHYSICAL 0x00000001 /* Flags: the pages belong to the creator */
#define MI_SECTION_SHARE_SOURCE 0x00000002 /* Flags: read only data section, pages are the file's own */
#define MI_SECTION_WRITABLE_WINDOW 0x00000004 /* Flags: the pages are the file's own and the views write them, see IoGetFileWindowEx */

typedef struct _SECTION {
    ULONG Flags;   /* SEC_IMAGE, SEC_COMMIT, ... */
    ULONG Protect; /* maximum protection of views */
    ULONG64 Size;
    ULONG PageCount;
    ULONG Pad;
    ULONG64 *Pages;       /* physical address of every prototype page */
    UCHAR *PageProtect;   /* image sections: MI protection index per page */
    PVOID SystemVa;       /* kernel mapping when mapped in system space */
    PFILE_OBJECT File;
    UNICODE_STRING FileName;
    MI_IMAGE_INFO Image;
    ULONG RelocRva;
    ULONG RelocSize;
    ULONG64 SourcePa;   /* read only file data the pages are built from: rom or host window */
    ULONG64 SourceSize; /* file bytes at SourcePa */
    PVOID SourceRef;    /* the window reference, see IoGetFileWindow */
    LIST_ENTRY WindowLinks; /* sections on a window of the host, see mm/winlost.c; empty links when not */
    volatile ULONG64 LostBegin, LostEnd; /* the pages of the window the host took away, as guest physical addresses */
    /*
     * images: for each page the offset of the base relocation block that fixes it, with a flag when the block
     * of the page before reaches into it; built when the first view away from the base is made.
     */
    ULONG *RelocIndex;
    ULONG RelocIndexState; /* 0 not tried, 1 built, 2 the table is not one the index can describe */
    ULONG_PTR RelocBase;   /* where the first view away from the preferred base was made, and where the next ones are */
    ULONG64 *RelocPages;   /* the relocated pages of the views at RelocBase, built as they are touched */
    ULONG ImageSlot;       /* images of files: one more than the place in the table of the images that processes share */
} SECTION, *PSECTION;

ULONG64 MiSectionPage(PSECTION s, ULONG index);
/* what the first touch of a page of a view maps; shared tells that the page belongs to the section. */
BOOLEAN MiSectionFaultPage(PMMVAD vad, ULONG_PTR page, ULONG protect, ULONG64 *pa, BOOLEAN *shared);
/* the page a view would map for a page without building or copying anything, or 0. */
ULONG64 MiSectionReadyPage(PMMVAD vad, ULONG64 index, ULONG protect);
ULONG64 MiSectionSwPte(ULONG protect);

/* winlost.c: pages of file windows the host took away. */
#define MmWindowPageLost(s, pa) ((pa) >= __atomic_load_n(&(s)->LostBegin, __ATOMIC_ACQUIRE) && (pa) < __atomic_load_n(&(s)->LostEnd, __ATOMIC_ACQUIRE))
void MmWindowSectionAttach(PSECTION s);
void MmWindowSectionDetach(PSECTION s);
void MmWindowLostInitialize(void);
void MmWindowLostInterrupt(void);
void MmWindowLostFault(void);
void FsCacheWindowLost(ULONG64 ipa, ULONG64 end);
ULONG64 MiSectionPteAttrs(PSECTION s, ULONG protect, ULONG64 pa, BOOLEAN shared);

void MmInitialize(MimeBootInfo *boot);
void MmInitializePhase1(void);
ULONG64 MmAllocatePage(void);           /* zeroed, returns physical address, 0 on failure */
ULONG64 MmAllocatePageNoZero(void);
void MmFreePage(ULONG64 pa);
void MmFreePages(const ULONG64 *pages, ULONG count);
void MmFreePagesQuiet(const ULONG64 *pages, ULONG count);
void MmFreeZeroedPagesQuiet(const ULONG64 *pages, ULONG count);
#define MM_BATCH_PAGES 16
ULONG MmAllocateZeroedPages(ULONG count, ULONG64 *pages); /* up to MM_BATCH_PAGES, returns how many */
ULONG64 MmAllocateContiguous(SIZE_T pages); /* zeroed run */
void MmFreeContiguous(ULONG64 pa, SIZE_T pages);
PMMPFN MiGetPfn(ULONG64 pa);
SIZE_T MmGetFreePages(void);
void MmDiscardIdle(void); /* idle loop, interrupts disabled: hands freed memory back to the host */
SIZE_T MmGetTotalPages(void);

ULONG64 *MiWalk(ULONG64 *root, ULONG_PTR va, BOOLEAN create, int level);
BOOLEAN MiMapPage(ULONG64 *root, ULONG_PTR va, ULONG64 pa, ULONG64 attrs);
ULONG64 MiUnmapPage(ULONG64 *root, ULONG_PTR va, USHORT asid);
ULONG64 MiReadPte(ULONG64 *root, ULONG_PTR va);
void MiWritePte(ULONG64 *root, ULONG_PTR va, ULONG64 pte, USHORT asid);
void MiFlushTlbPage(ULONG_PTR va, USHORT asid, BOOLEAN global);
void MiFlushTlbRange(ULONG_PTR va, SIZE_T pages, USHORT asid);
void MiRewritePtes(ULONG64 *pte, ULONG_PTR va, SIZE_T count, const ULONG64 *values, USHORT asid);
void MiClearPtes(ULONG64 *pte, ULONG_PTR va, SIZE_T count, USHORT asid, ULONG64 *old);
void MiUpgradePte(ULONG64 *root, ULONG_PTR va, ULONG64 value, USHORT asid);
void MiFlushTlbAsid(USHORT asid);
void MiFlushTlbAll(void);
ULONG64 MiProtectToPte(ULONG protect, BOOLEAN user);
ULONG MiPteToProtect(ULONG64 pte);
ULONG64 *MiKernelRoot(void);
ULONG64 MiCreateSystemRoot(void);
void MiInitializeProcessRoot(ULONG64 *root);
ULONG64 MiDirectoryTableBase(PMMADDRESS_SPACE as);

PVOID MmAllocateSystemVa(SIZE_T pages);
void MmFreeSystemVa(PVOID va, SIZE_T pages);
PVOID MmMapPagesInSystem(const ULONG64 *pages, SIZE_T count, ULONG64 attrs);
PVOID MmAllocateKernelStack(SIZE_T size, PVOID *limit);
void MmFreeKernelStack(PVOID allocation, SIZE_T size);

void ExInitializePool(void);
SIZE_T ExQueryPoolUsage(void);

NTSTATUS MmCreateAddressSpace(PMMADDRESS_SPACE as);
void MmDeleteAddressSpace(PMMADDRESS_SPACE as);
PMMVAD MiFindVad(PMMADDRESS_SPACE as, ULONG_PTR va);
NTSTATUS MiInsertVad(PMMADDRESS_SPACE as, PMMVAD vad);
NTSTATUS MiFindFreeRange(PMMADDRESS_SPACE as, SIZE_T size, ULONG_PTR alignment, BOOLEAN top_down,
                         ULONG_PTR *out);
NTSTATUS MiFindFreeRangeEx(PMMADDRESS_SPACE as, SIZE_T size, ULONG_PTR alignment, ULONG_PTR lowest,
                           ULONG_PTR highest, BOOLEAN top_down, ULONG_PTR *out);
BOOLEAN MiRangeIsFree(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end);
void MiRemoveVad(PMMADDRESS_SPACE as, PMMVAD vad);
/* what the extended parameters of an allocation or a view ask for. */
typedef struct _MI_EXTENDED {
    ULONG_PTR Lowest;
    ULONG_PTR Highest;
    ULONG_PTR Alignment;
    ULONG Node; /* the preferred node plus one, or 0 */
    ULONG Present; /* a bit for each type that was given */
    ULONG64 Attributes;
    HANDLE Partition;
    HANDLE PhysicalHandle;
    USHORT Machine;
} MI_EXTENDED;

BOOLEAN MiProtectionIsValid(ULONG protect);
NTSTATUS MiCaptureExtendedParameters(const MEM_EXTENDED_PARAMETER *parameters, ULONG count, ULONG allowed,
                                     BOOLEAN user, MI_EXTENDED *out);
NTSTATUS MiAllocateVirtualEx(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type, ULONG protect,
                             ULONG_PTR zero_bits, const MI_EXTENDED *extended);
ULONG MmQueryVmCounters(PEPROCESS process, PVOID out, ULONG length);
SIZE_T MmGetCommittedPages(void);
SIZE_T MmGetPeakCommittedPages(void);
SIZE_T MmGetCommitLimitPages(void);
NTSTATUS MiAllocateVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type, ULONG protect);
NTSTATUS MiFreeVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type);
NTSTATUS MiProtectVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG protect, PULONG old);
NTSTATUS MiQueryVirtual(PEPROCESS process, PVOID base, PMEMORY_BASIC_INFORMATION info);
BOOLEAN MmAccessFault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute, KPROCESSOR_MODE mode);
NTSTATUS MiCreateImageSection(PFILE_OBJECT file, PSECTION *out);
NTSTATUS MiCreateDataSection(PFILE_OBJECT file, ULONG64 size, ULONG protect, ULONG flags, PSECTION *out);
NTSTATUS MiMapViewOfSection(PSECTION section, PEPROCESS process, PVOID *base, ULONG64 offset, SIZE_T *view_size,
                            ULONG protect, ULONG allocation_type);
NTSTATUS MiMapViewOfSectionEx(PSECTION section, PEPROCESS process, PVOID *base, ULONG64 offset, SIZE_T *view_size,
                              ULONG protect, ULONG allocation_type, ULONG_PTR zero_bits, const MI_EXTENDED *ext);
NTSTATUS MiUnmapViewOfSection(PEPROCESS process, PVOID base);
NTSTATUS MiUnmapViewOfSectionEx(PEPROCESS process, PVOID base, ULONG flags);
SIZE_T MiReleasePages(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end);
void MiSwapVad(PMMADDRESS_SPACE as, PMMVAD old, PMMVAD fresh);
NTSTATUS MiZeroBitsHighest(ULONG_PTR zero_bits, ULONG_PTR *highest);
NTSTATUS MiParseImageHeaders(const void *headers, SIZE_T length, MI_IMAGE_INFO *info);
NTSTATUS MmLoadSystemImage(PCUNICODE_STRING path, PVOID *base, SIZE_T *size, PVOID *entry);
typedef void (*MI_SYSTEM_IMAGE_CALLBACK)(PVOID context, ULONG index, PVOID base, SIZE_T size, const WCHAR *name);
ULONG MiEnumerateSystemImages(MI_SYSTEM_IMAGE_CALLBACK callback, PVOID context);
PVOID MiFindKernelExport(PVOID image_base, const char *name);
NTSTATUS MiReadFileBytes(PFILE_OBJECT file, ULONG64 offset, PVOID buffer, ULONG length, PULONG read);

/* ------------------------------------------------------------------ ob */

typedef struct _OBJECT_TYPE {
    UNICODE_STRING Name;
    WCHAR NameBuffer[32];
    OBJECT_TYPE_INITIALIZER Init;
    ULONG Index;
    volatile LONG TotalObjects;
    volatile LONG TotalHandles;
} OBJECT_TYPE;

/* the rights a type means by the generic ones in a mask, replacing them. */
ACCESS_MASK ObMapGenericAccess(POBJECT_TYPE type, ACCESS_MASK mask);
NTSTATUS ObQueryAllTypesInformation(PVOID info, ULONG length, PULONG returned);

typedef struct _OBJECT_HEADER {
    volatile LONG_PTR PointerCount;
    volatile LONG_PTR HandleCount;
    POBJECT_TYPE Type;
    struct _OBJECT_DIRECTORY *Directory;
    UNICODE_STRING Name;
    ULONG Flags;
    ULONG Pad;
    ULONG64 Reserved;
} OBJECT_HEADER;
MIME_STATIC_ASSERT(sizeof(OBJECT_HEADER) == 64, "OBJECT_HEADER");

#define OB_FLAG_PERMANENT 0x1
#define OB_FLAG_KERNEL 0x2
#define OB_FLAG_SD_PENDING 0x4 /* made, no security descriptor assigned yet */

#define OBJECT_TO_HEADER(o) ((OBJECT_HEADER *)((PUCHAR)(o) - sizeof(OBJECT_HEADER)))
#define HEADER_TO_OBJECT(h) ((PVOID)((PUCHAR)(h) + sizeof(OBJECT_HEADER)))

#define OB_DIRECTORY_BUCKETS 37

typedef struct _OBJECT_DIRECTORY_ENTRY {
    struct _OBJECT_DIRECTORY_ENTRY *Next;
    PVOID Object;
} OBJECT_DIRECTORY_ENTRY;

typedef struct _OBJECT_DIRECTORY {
    OBJECT_DIRECTORY_ENTRY *Buckets[OB_DIRECTORY_BUCKETS];
} OBJECT_DIRECTORY, *POBJECT_DIRECTORY;

typedef struct _OBJECT_SYMBOLIC_LINK {
    UNICODE_STRING Target;
} OBJECT_SYMBOLIC_LINK, *POBJECT_SYMBOLIC_LINK;

typedef struct _HANDLE_TABLE_ENTRY {
    PVOID Object; /* object body */
    ACCESS_MASK GrantedAccess;
    ULONG Attributes;
} HANDLE_TABLE_ENTRY, *PHANDLE_TABLE_ENTRY;

typedef struct _HANDLE_TABLE {
    PHANDLE_TABLE_ENTRY Entries;
    ULONG Capacity;
    ULONG Count;
    ULONG FirstFree;
    KSPIN_LOCK Lock;
} HANDLE_TABLE, *PHANDLE_TABLE;

extern POBJECT_TYPE ObDirectoryType;
extern POBJECT_TYPE ObSymbolicLinkType;
extern POBJECT_TYPE ObTypeType;
extern POBJECT_DIRECTORY ObRootDirectory;

void ObInitialize(void);
void ObpInitializeSecurity(void);
void ObpFreeSecurity(PVOID header);

/*
 * security descriptors the kernel keeps, always self relative and shared by reference count: the parts an object
 * has, the stored one of a file or key, the default of a token. A blob is never changed once made.
 */
typedef struct _SD_BLOB {
    volatile LONG Refs;
    ULONG Length;
    UCHAR Data[1];
} SD_BLOB;

#define SD_MAX_LENGTH 0x10000
#define ACCESS_SYSTEM_SECURITY 0x01000000

/* a right the descriptor of an object grants for the generic ones, read, write, execute and all. */
typedef const ULONG *SD_MAPPING;

SD_BLOB *SeSdAllocate(ULONG length);
SD_BLOB *SeSdReference(SD_BLOB *sd);
void SeSdDereference(SD_BLOB *sd);
/* the blob of a descriptor of the caller, absolute or self relative, checked; STATUS_INVALID_SECURITY_DESCR. */
NTSTATUS SeSdCapture(const void *descriptor, KPROCESSOR_MODE mode, SD_BLOB **out);
/* a blob made of the bytes of a self relative descriptor, which must be valid. */
NTSTATUS SeSdFromBytes(const void *descriptor, ULONG length, SD_BLOB **out);
/* what a new object of the calling thread gets from its parent and its creator, both optional. */
NTSTATUS SeSdAssign(const SD_BLOB *parent, const SD_BLOB *creator, SD_MAPPING mapping, BOOLEAN container,
                    KPROCESSOR_MODE mode, SD_BLOB **out);
/* a child of a parent as the user of the machine would have made it, which is how an untouched file or key shows. */
NTSTATUS SeSdDerive(const SD_BLOB *parent, SD_MAPPING mapping, BOOLEAN container, SD_BLOB **out);
/* whether the calling thread makes things the way the user of the machine does, with nothing to record. */
BOOLEAN SeSdSubjectIsDefault(void);
/* the same with nothing from the creator or the parent, for a thread acting as one token: cached. */
NTSTATUS SeSdDefault(SD_MAPPING mapping, BOOLEAN container, SD_BLOB **out);
/* what an object nobody gave a descriptor gets: the system and the administrators own it. */
NTSTATUS SeSdSystemDefault(SD_MAPPING mapping, BOOLEAN container, SD_BLOB **out);
/* a descriptor changed by the parts of the information; the checks of the kernel for a caller in user mode. */
NTSTATUS SeSdSet(ULONG information, const SD_BLOB *modification, const SD_BLOB *old, SD_MAPPING mapping,
                 KPROCESSOR_MODE mode, SD_BLOB **out);
/* the parts of the information in the layout of NtQuerySecurityObject; *length is the room, then what is needed. */
NTSTATUS SeSdQuery(ULONG information, const SD_BLOB *sd, PVOID buffer, PULONG length);
/* the rights a handle needs to read or change the parts of the information. */
ACCESS_MASK SeSdReadRights(ULONG information);
ACCESS_MASK SeSdWriteRights(ULONG information);

/* a descriptor from its parts, for the defaults of the kernel; the ace specs are allow aces of the sids. */
typedef struct _SD_ACE {
    UCHAR Flags;
    ULONG Mask;
    const UCHAR *Sid;
} SD_ACE;
NTSTATUS SeSdBuild(const UCHAR *owner, const UCHAR *group, USHORT control, const SD_ACE *dacl, ULONG dacl_count,
                   SD_BLOB **out);
/* the well known sids, 12 to 28 bytes each. */
extern const UCHAR SeSidSystem[12], SeSidAdministrators[16], SeSidUsers[16], SeSidEveryone[12],
    SeSidAuthenticatedUsers[12], SeSidCreatorOwner[12], SeSidTrustedInstaller[32], SeSidAllAppPackages[16],
    SeSidRestrictedPackages[16];

/* the descriptors of the files of mimefs, io/mimefs_sec.c. */
NTSTATUS MimeFsRomSecurity(SD_BLOB **out);
NTSTATUS MimeFsEffectiveSecurity(ULONG volume, ULONG64 host, PCUNICODE_STRING path, ULONG up, BOOLEAN directory,
                                 SD_BLOB **out);
NTSTATUS MimeFsStoreSecurity(ULONG64 host, SD_BLOB *sd);
NTSTATUS MimeFsAssignNewFile(ULONG volume, ULONG64 host, PCUNICODE_STRING path, BOOLEAN directory, SD_BLOB *creator,
                             KPROCESSOR_MODE mode);

/* the descriptor a new object of a type would get, made before the object so a refusal makes nothing. */
NTSTATUS ObComputeSecurity(POBJECT_TYPE type, const OBJECT_ATTRIBUTES *attributes, PVOID parent, KPROCESSOR_MODE mode,
                           SD_BLOB **sd);
void ObAttachSecurity(PVOID object, SD_BLOB *sd);
/* stores the descriptor of a new object, from the attributes of its creation and the directory it joins. */
NTSTATUS ObAssignSecurity(PVOID object, const OBJECT_ATTRIBUTES *attributes, PVOID parent, KPROCESSOR_MODE mode);
/* the descriptor of an object, whole, in a blob of its own (the object's or its file system's). */
NTSTATUS ObReferenceSecurity(PVOID object, SD_BLOB **sd);
NTSTATUS ObStoreSecurity(PVOID object, SD_BLOB *sd);
NTSTATUS ObCreateHandle(PEPROCESS process, PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle);
NTSTATUS ObInheritHandles(PEPROCESS parent, PEPROCESS child);
NTSTATUS ObInheritHandleList(PEPROCESS parent, PEPROCESS child, const HANDLE *list, ULONG count);
NTSTATUS ObCloseHandle(HANDLE handle, KPROCESSOR_MODE mode);
NTSTATUS ObLookupObjectName(PUNICODE_STRING name, HANDLE root, ULONG attributes, POBJECT_TYPE type,
                            KPROCESSOR_MODE mode, PVOID parse_context, PVOID *object);
NTSTATUS ObInsertIntoDirectory(POBJECT_DIRECTORY dir, PVOID object, PCUNICODE_STRING name);
NTSTATUS ObCreateDirectory(PCWSTR path, POBJECT_DIRECTORY *out);
NTSTATUS ObCreateSymbolicLink(PCWSTR link, PCWSTR target);
NTSTATUS ObInitializeHandleTable(PHANDLE_TABLE table);
void ObKillProcessHandles(PEPROCESS process);
NTSTATUS ObDuplicateHandle(PEPROCESS source_process, HANDLE source, PEPROCESS target_process, PHANDLE target,
                           ACCESS_MASK access, ULONG attributes, ULONG options);
PDISPATCHER_HEADER ObGetWaitObject(PVOID object);
NTSTATUS ObCreateSymbolicLinkObject(PCUNICODE_STRING link, PCUNICODE_STRING target, BOOLEAN permanent);
void ObMakeTemporaryObject(PVOID object);

/* ------------------------------------------------------------------ ps */

typedef struct _ETHREAD {
    KTHREAD Tcb;
    CLIENT_ID Cid;
    PVOID StartAddress;
    PVOID Win32StartAddress;
    NTSTATUS ExitStatus;
    BOOLEAN SystemThread;
    BOOLEAN HasTerminated;
    BOOLEAN Initialized;
    BOOLEAN DeadThread;
    LIST_ENTRY ThreadListEntry; /* in EPROCESS */
    PVOID UserStackAllocation;
    SIZE_T UserStackSize;
    ULONG_PTR UserStackGuard; /* the guard page of the stack, the first touch of which is a stack overflow */
    PVOID NativeStackAllocation; /* the translator's own stack in x86 and x64 processes */
    SIZE_T NativeStackSize;
    ULONG64 CreateTime;
    ULONG64 ExitTime;
    LONG_PTR SameThreadApcFlags;
    struct _TOKEN *ImpersonationToken; /* referenced, NULL when the thread acts as its process */
    UCHAR ImpersonationLevel;
    BOOLEAN CopyOnOpen;
    BOOLEAN EffectiveOnly;
    UCHAR CreateQos;            /* impersonation level asked by the file being created plus one, 0 for none */
    BOOLEAN CreateQosEffective;
    BOOLEAN DynamicCodeAllowed; /* ThreadDynamicCodePolicyInfo: this thread may opt out of a ban on dynamic code */
    BOOLEAN HideFromDebugger;
} ETHREAD;

#define MIME_IMAGE_NAME_CHARS 260

/*
 * the dll the kernel maps into every process and enters user mode
 * through: ntdll.dll for arm64 programs, the translator mimex86.dll for
 * x86 and x64 ones. its exports are cached when it is loaded at boot.
 */
typedef struct _PSP_SYSTEM_DLL {
    PSECTION Section;
    PVOID Base;
    PVOID LdrInitializeThunk;
    PVOID RtlUserThreadStart;
    PVOID KiUserExceptionDispatcher;
    PVOID KiUserApcDispatcher;
    PVOID KiUserCallbackDispatcher;
} PSP_SYSTEM_DLL, *PPSP_SYSTEM_DLL;

typedef struct _EPROCESS {
    KPROCESS Pcb;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    LIST_ENTRY ActiveProcessLinks;
    LIST_ENTRY ThreadListHead;
    volatile LONG ThreadCount;
    HANDLE_TABLE HandleTable;
    MMADDRESS_SPACE AddressSpace;
    PPEB Peb;
    PVOID SectionBaseAddress;
    PSECTION ImageSection;
    PVOID NtdllBase;            /* base of the system dll */
    PPSP_SYSTEM_DLL SystemDll;
    USHORT Machine;             /* of the program image */
    PVOID Wow64Peb;             /* the 32-bit peb of a wow64 process */
    CHAR ImageFileName[16];
    WCHAR ImagePathBuffer[MIME_IMAGE_NAME_CHARS];
    UNICODE_STRING ImagePath;
    NTSTATUS ExitStatus;
    BOOLEAN Terminating;
    BOOLEAN Exited;
    BOOLEAN SystemProcess;
    BOOLEAN ParametersInitialized;
    PVOID Win32Process;
    PVOID KernelCallbackTable;
    ULONG64 MimeRequestId; /* host run request that created the process */
    struct _MIME_CONSOLE *Console; /* the console the process is attached to, io/console.c */
    PVOID ConsoleCtrlRoutine;      /* kernelbase's, started on a new thread for each ctrl event */
    ULONG ConsoleGroup;            /* process group of GenerateConsoleCtrlEvent */
    PKEVENT ConsoleInputEvent;     /* kernelbase's, set while console input waits */
    ULONG TimerResolution; /* what NtSetTimerResolution asked for, 0 when nothing, ex/sysinfo.c */
    UCHAR PriorityClass;   /* PROCESS_PRIORITY_CLASS_*, ps/priority.c */
    BOOLEAN Foreground;
    BOOLEAN DisableBoost;
    ULONG64 CreateTime;
    ULONG64 ExitTime;
    KEVENT ExitEvent;
    struct EJOB *Job; /* ps/job.c */
    LIST_ENTRY JobLinks;
    struct _TOKEN *Token; /* the primary token, referenced */
    BOOLEAN TokenFrozen;  /* the first thread has run, the token is for good */
    /* ps/mitigation.c */
    ULONG64 MitigationMap[3];                     /* the options it was created with, in the kernel's layout */
    ULONG64 MitigationFlags[2];                   /* PROCESS_CREATION_MITIGATION_POLICY and POLICY2 of them */
    ULONG MitigationPolicy[MIME_POLICY_COUNT];    /* the flag word of each PROCESS_MITIGATION_POLICY class */
    ULONG ChildProcessPolicy;                     /* PROCESS_CREATION_CHILD_PROCESS_* */
    ULONG Win32kFilter[2];
    ULONG AllApplicationPackagesPolicy, DesktopAppPolicy, ComponentFilter;
    /* ps/shutdown.c */
    ULONG ShutdownLevel; /* SetProcessShutdownParameters, 0x280 until it is called */
    ULONG ShutdownFlags; /* SHUTDOWN_NORETRY */
    LONG ShutdownState;  /* PS_SHUTDOWN_* of the shutdown in progress */
    BOOLEAN ServiceController; /* services.exe, which the shutdown gives the time of the services */
    ULONG SessionId;           /* of the token it was made with, 0 for the services and the system */
} EPROCESS;

extern LIST_ENTRY PsActiveProcessHead;
void ExReleaseTimerResolution(PEPROCESS process);
extern PEPROCESS PsIdleProcess;
extern POBJECT_TYPE PsProcessTypeObject;
extern POBJECT_TYPE PsThreadTypeObject;

/* a 32-bit program on x64 runs natively on the 64-bit ntdll and wow64.dll, as on windows; arm64 translates it. */
FORCEINLINE BOOLEAN PspIsWow64Process(PEPROCESS process)
{
    return MIME_NATIVE_MACHINE == IMAGE_FILE_MACHINE_AMD64 && process->Machine == IMAGE_FILE_MACHINE_I386;
}

FORCEINLINE PEPROCESS PsCurrentProcess(void)
{
    return (PEPROCESS)KiCurrentThread()->Process;
}

FORCEINLINE PETHREAD PsCurrentThread(void)
{
    return (PETHREAD)KiCurrentThread();
}

void PsInitialize(void);
void PsInitializeIdleThread(void);
PVOID PsCreateIdleThread(PKPRCB prcb);
NTSTATUS PspCreateThread(PEPROCESS process, BOOLEAN system, PKSTART_ROUTINE routine, PVOID context,
                         PETHREAD *out);
NTSTATUS PspCreateUserThread(PEPROCESS process, PVOID start, PVOID parameter, BOOLEAN suspended,
                             SIZE_T stack_reserve, SIZE_T stack_commit, PETHREAD *out);
typedef struct _MIME_CONSOLE MIME_CONSOLE;
NTSTATUS PsCreateUserProcess(PCUNICODE_STRING image_dos_path, PCUNICODE_STRING command_line,
                             PCUNICODE_STRING current_directory, ULONG64 request_id, MIME_CONSOLE *console,
                             BOOLEAN elevated, PEPROCESS *out);
/* the service control manager, a process of the system that nobody asked the host for. */
NTSTATUS PsStartServiceController(void);
DECLSPEC_NORETURN void PspExitThread(NTSTATUS status);
NTSTATUS PspTerminateProcess(PEPROCESS process, NTSTATUS status);
/* ps/mitigation.c */
void PspApplyMitigationOptions(PEPROCESS process, PEPROCESS parent, const ULONG64 requested[3]);
void PspApplyChildProcessPolicy(PEPROCESS process, ULONG policy);
NTSTATUS PspSetMitigationPolicy(HANDLE handle, PVOID info, ULONG length);
NTSTATUS PspQueryMitigationPolicy(PEPROCESS process, PVOID info, ULONG length, PULONG returned);
NTSTATUS PspSetThreadDynamicCodePolicy(PETHREAD thread, PVOID info, ULONG length);
BOOLEAN PsDynamicCodeBlocked(PEPROCESS target);
#define PROCESS_CREATE_FLAGS_BREAKAWAY 0x00000001
void PspInitializeJobs(void);
NTSTATUS PspInheritJob(PEPROCESS process, PEPROCESS parent, ULONG flags);
NTSTATUS PspReferenceJobForCreation(const HANDLE *list, ULONG count, struct EJOB **job);
NTSTATUS PspAssignNewProcessToJob(struct EJOB *job, PEPROCESS process);
void PspJobProcessExit(PEPROCESS process);
void PsDumpThreads(void);
void KiTimelineNote(ULONG kind, const char *text, ULONG64 detail);
extern BOOLEAN KiTimelineOn;
void NpfsDumpState(void);
PVOID KiSatisfiedWaitObject(PKTHREAD thread);
void KiDumpServiceCalls(void);
extern POBJECT_TYPE IoCompletionObjectType;
PEPROCESS PspLookupProcess(HANDLE pid);
PETHREAD PspLookupThread(HANDLE tid);
NTSTATUS PspAllocateId(PVOID object, PHANDLE id);
void PspFreeId(HANDLE id);
NTSTATUS PsReferenceProcessHandle(HANDLE handle, ACCESS_MASK access, PEPROCESS *process);
NTSTATUS PsReferenceThreadHandle(HANDLE handle, ACCESS_MASK access, PETHREAD *thread);
void PspReapDeadThreads(void);
extern PSP_SYSTEM_DLL PspNativeSystemDll;
extern PSP_SYSTEM_DLL PspX86SystemDll;
NTSTATUS PspInitializeSystemDll(void);
PPSP_SYSTEM_DLL PspSystemDllForMachine(USHORT machine);
extern PSECTION PspWow64NtdllSection;

/* ------------------------------------------------------------------ se */

/* tokens, se/token.c: the primary token of a process and the identity of a thread. */
void SepInitialize(void);
NTSTATUS SepAssignProcessToken(PEPROCESS child, PEPROCESS parent);
void SepSetProcessToken(PEPROCESS process, struct _TOKEN *token);
struct _TOKEN *SepCreateServiceControllerToken(void);
struct _TOKEN *SepCreateElevatedUserToken(void);
ULONG SepTokenSessionId(PEPROCESS process);
void SepFreeProcessToken(PEPROCESS process);
void SepFreeThreadToken(PETHREAD thread);
struct _TOKEN *SepReferenceEffectiveToken(PETHREAD thread);
NTSTATUS SepImpersonateClient(PETHREAD thread, struct _TOKEN *token, BOOLEAN copy_on_open, BOOLEAN effective_only,
                              ULONG level);
NTSTATUS SepSetThreadToken(HANDLE thread_handle, PVOID info, ULONG length);
NTSTATUS SepSetProcessAccessToken(HANDLE process_handle, PVOID info, ULONG length);
NTSTATUS SepReferenceTokenForNewProcess(HANDLE handle, struct _TOKEN **out);
struct _TOKEN *SepDuplicateForPipe(struct _TOKEN *token, ULONG level, BOOLEAN effective_only);

/* ------------------------------------------------------------------ io */

extern POBJECT_TYPE IoDeviceObjectType;
extern POBJECT_TYPE IoDriverObjectType;
extern POBJECT_TYPE IoFileObjectTypeObject;

void IoInitialize(void);
NTSTATUS IoCreateDriver(PCWSTR name, PDRIVER_INITIALIZE init, PDRIVER_OBJECT *out);
NTSTATUS IopCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                       ULONG file_attributes, ULONG share, ULONG disposition, ULONG options, KPROCESSOR_MODE mode);
NTSTATUS IopCreateFileEx(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                         ULONG file_attributes, ULONG share, ULONG disposition, ULONG options, KPROCESSOR_MODE mode,
                         BOOLEAN stat_only);
NTSTATUS IopOpenFileObject(PCUNICODE_STRING path, ACCESS_MASK access, ULONG options, PFILE_OBJECT *out);
NTSTATUS IopSynchronousRequest(PFILE_OBJECT file, UCHAR major, UCHAR minor, PVOID buffer, ULONG length,
                               PIO_STACK_LOCATION params, PIO_STATUS_BLOCK iosb);
NTSTATUS IoRenameFileObject(PFILE_OBJECT file, PCWSTR new_name, BOOLEAN replace);
const MimeRomEntry *IoGetRomFile(PFILE_OBJECT file);
BOOLEAN IoGetFileWindow(PFILE_OBJECT file, PULONG64 pa, PULONG64 size, PVOID *ref);
/* the same, for a section of a data file: a window that is the file's own pages in both directions. writable asks for one
   the guest may write, which makes the file at least minimum_size long first, as windows does for a section. */
BOOLEAN IoGetFileWindowEx(PFILE_OBJECT file, BOOLEAN writable, ULONG64 minimum_size, PULONG64 pa, PULONG64 size, PVOID *ref);
void IoReleaseFileWindow(PVOID ref);

NTSTATUS PspBuildEnvironment(USHORT machine, PWCHAR *block, SIZE_T *bytes);

/* cm, the configuration manager. */
extern POBJECT_TYPE CmKeyType;
void CmInitSystem1(void);
/* the thread ends: what it waits for in the registry is answered with the clean up status. */
void CmNotifyRunDown(PETHREAD thread);
/* makes the registry journal durable, as the machine powers off. */
void CmpJournalShutdown(void);
NTSTATUS CmQueryValue(PCWSTR key_path, PCWSTR name, PULONG type, PVOID data, PULONG length);
NTSTATUS CmSetValue(PCWSTR key_path, PCWSTR name, ULONG type, const void *data, ULONG length);
NTSTATUS CmEnumerateValues(PCWSTR key_path,
                           void (*fn)(PVOID ctx, PCWSTR name, ULONG name_chars, ULONG type, const void *data, ULONG length),
                           PVOID ctx);

/* io/fscache.c, the file data cache of the host backed volumes. */
typedef struct _MIMEFS_CACHE MIMEFS_CACHE;
void FsCacheInitialize(void);
MIMEFS_CACHE *FsCacheOpen(ULONG volume, const MimeFsInfo *info);
void FsCacheClose(MIMEFS_CACHE *cache);
void FsCacheInvalidate(ULONG volume, ULONG64 file_id);
BOOLEAN FsCacheRead(MIMEFS_CACHE *cache, ULONG64 host_handle, ULONG64 offset, PUCHAR out, ULONG length, PULONG done);
BOOLEAN FsCacheGetWindow(MIMEFS_CACHE *cache, ULONG64 host_handle, PULONG64 pa, PULONG64 size);
/* a window of a host handle that belongs to its user, shared with no one; unmapped by FsCacheUnmapPrivate. */
BOOLEAN FsCacheMapPrivate(ULONG64 host_handle, BOOLEAN writable, PULONG64 pa);
void FsCacheUnmapPrivate(ULONG64 pa);
/* mm/virtual.c: how much of a copy into another process's memory was done through its present pages, see there. */
SIZE_T MmCopyToProcessFast(PEPROCESS process, PVOID dst, const void *src, SIZE_T length);
/* copies into the memory of a process, the current one or another, from any thread. */
NTSTATUS IopCopyToProcess(PEPROCESS process, PVOID dst, const void *src, SIZE_T length);

/* io/fswrite.c, the small writes of a host backed file kept in the guest and written to the host together. */
typedef struct _FS_WB {
    LIST_ENTRY Links;    /* of the kept buffers, while this one holds one */
    PUCHAR Buffer;
    ULONG64 Start;       /* the file offset of Buffer[0] */
    ULONG Length;        /* bytes of the buffer the host does not have yet */
    ULONG Idle;          /* ticks of the flusher that found it clean */
    ULONG Volume;
    ULONG64 FileId;
    ULONG64 Host;        /* the host handle the flushes write to */
    PCUNICODE_STRING Name;
    NTSTATUS Failed;     /* the first flush that failed, told to every write and flush of the handle after it: none lands past the gap */
    BOOLEAN Kept;
} FS_WB;
NTSTATUS MimeFsHostStatus(ULONG64 host_status, BOOLEAN path_missing);
void FsWbInitialize(void);
void FsWbOpen(FS_WB *wb, ULONG volume, ULONG64 file_id, ULONG64 host, PCUNICODE_STRING name);
/* takes a write into the buffer; FALSE when it must go to the host as it is. */
BOOLEAN FsWbWrite(FS_WB *wb, ULONG64 offset, const void *data, ULONG length, NTSTATUS *status);
/* writes what this handle keeps, and what the other handles of the file keep, to the host. */
NTSTATUS FsWbSync(FS_WB *wb);
/* the same for the other handles of the file only. */
void FsWbSyncOthers(FS_WB *wb);
/* what handles keep for the file of this name, or for the files below it when it is a directory. */
void FsWbSyncPath(ULONG volume, PCUNICODE_STRING path, BOOLEAN directory);
/* held while a file object's name is replaced, which the matching by name reads. */
void FsWbNameLock(void);
void FsWbNameUnlock(void);
/* writes the handle's buffer out and gives it back. */
void FsWbClose(FS_WB *wb);
/* everything kept, for the machine's shutdown. */
void FsWbFlushAll(void);
/* a file with a section or a window of it in use keeps no writes back, see io/fswrite.c. */
void FsWbMapped(ULONG volume, ULONG64 file_id, LONG delta);
BOOLEAN FsWbIsMapped(ULONG volume, ULONG64 file_id);
BOOLEAN FsWbAnyMapped(void);
NTSTATUS IoReadFileObject(PFILE_OBJECT file, PVOID buffer, ULONG length, ULONG64 offset, PULONG read);
void IopInitializeCompletion(void);
void IopInitializeNamedPipes(void);
void IopQueueCompletionApc(PETHREAD thread, PVOID routine, PVOID context, PIO_STATUS_BLOCK iosb);
/* wow64 marks a 32-bit status block with bit 0 of the apc routine; inside, bit 0 of the block's address carries it. */
void IopTakeIosb32(PVOID *apc_routine, PIO_STATUS_BLOCK *iosb);
ULONG IopIosbImage(PIO_STATUS_BLOCK iosb, NTSTATUS status, ULONG_PTR information, PVOID *address, ULONG64 image[2]);
BOOLEAN IopIsNamedPipe(PFILE_OBJECT file);
NTSTATUS NpfsReadWrite(PFILE_OBJECT file, BOOLEAN write, HANDLE event, PVOID apc_routine, PVOID apc_context,
                       PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length);
NTSTATUS NpfsFsControl(PFILE_OBJECT file, HANDLE event, PVOID apc_routine, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                       ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len);
NTSTATUS NpfsCancel(PFILE_OBJECT file, PEPROCESS process, BOOLEAN one, PVOID apc_context);
void IopInitializeAfd(void);
void AfdInterrupt(void);
BOOLEAN IopIsAfd(PFILE_OBJECT file);
NTSTATUS AfdDeviceControl(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                          ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len);
NTSTATUS AfdReadWrite(PFILE_OBJECT file, BOOLEAN write, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                      PVOID buffer, ULONG length);
NTSTATUS AfdCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb);

/* request.c: a request of user mode a driver answers itself, maybe later. */
typedef struct IO_USER_REQUEST {
    LIST_ENTRY Links;  /* for the driver's queues */
    PFILE_OBJECT File;
    PEPROCESS Process;
    PETHREAD Thread;
    PKEVENT Event;
    PVOID ApcRoutine, ApcContext;
    PIO_STATUS_BLOCK Iosb;
    PVOID Buffer;      /* the caller's */
    ULONG Length;
    BOOLEAN Waiter;    /* a synchronous caller sleeps on Done and frees the request */
    BOOLEAN Immediate; /* completed before its call returned */
    KEVENT Done;
    NTSTATUS Status;
    ULONG_PTR Information;
} IO_USER_REQUEST;

IO_USER_REQUEST *IopNewUserRequest(PFILE_OBJECT file, HANDLE event, PVOID apc_routine, PVOID apc_context,
                                   PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, NTSTATUS *status);
NTSTATUS IopCopyToRequester(IO_USER_REQUEST *r, PVOID dst, const void *src, SIZE_T length);
void IopCompleteUserRequest(IO_USER_REQUEST *r, NTSTATUS status, ULONG_PTR information);
NTSTATUS IopImmediateUserRequest(IO_USER_REQUEST *r, NTSTATUS status, ULONG_PTR information);
NTSTATUS IopPendUserRequest(IO_USER_REQUEST *r);
/* fsnotify.c: the changes of the files of a volume that the directories being watched are told of. */
#define FILE_NOTIFY_CHANGE_FILE_NAME 0x00000001
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x00000002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x00000004
#define FILE_NOTIFY_CHANGE_SIZE 0x00000008
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x00000010
#define FILE_NOTIFY_CHANGE_LAST_ACCESS 0x00000020
#define FILE_NOTIFY_CHANGE_CREATION 0x00000040
#define FILE_NOTIFY_CHANGE_SECURITY 0x00000100
#define FILE_NOTIFY_VALID_MASK 0x00000FFF
#define FILE_ACTION_ADDED 0x00000001
#define FILE_ACTION_REMOVED 0x00000002
#define FILE_ACTION_MODIFIED 0x00000003
#define FILE_ACTION_RENAMED_OLD_NAME 0x00000004
#define FILE_ACTION_RENAMED_NEW_NAME 0x00000005
void FsNotifyInitialize(void);
void FsNotifyReport(ULONG volume, PCUNICODE_STRING path, ULONG action, ULONG filter);
void FsNotifyRename(ULONG volume, PCUNICODE_STRING from, PCUNICODE_STRING to, BOOLEAN directory);
void FsNotifyCleanup(PFILE_OBJECT file);
/* the host has changes of files in watched directories to fetch. */
void FsNotifyHostInterrupt(void);
NTSTATUS FsNotifyCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb);
/* mimefs.c: whether a file object is of the host backed volumes, and the volume of an open directory. */
BOOLEAN IopIsMimeFs(PFILE_OBJECT file);
NTSTATUS MimeFsDirectoryVolume(PFILE_OBJECT file, PULONG volume, PULONG64 host_handle);

/* hid.c: game controllers' hid and xusb devices, answering their files' requests. */
BOOLEAN IopIsPad(PFILE_OBJECT file);
NTSTATUS PadReadWrite(PFILE_OBJECT file, BOOLEAN write, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                      PVOID buffer, ULONG length);
NTSTATUS PadDeviceControl(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb, ULONG code,
                          PVOID input, ULONG in_len, PVOID output, ULONG out_len);
NTSTATUS PadCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb);

/* hidparse.c: a report descriptor's top level collections, each with its preparsed data, and its report ids. */
typedef struct HIDP_COLLECTION {
    USHORT UsagePage, Usage;
    USHORT InputLength, OutputLength, FeatureLength; /* with the id byte */
    USHORT PreparsedLength;
    PUCHAR Preparsed;
} HIDP_COLLECTION;

typedef struct HIDP_REPORT_ID {
    UCHAR ReportId, Collection;
    USHORT InputLength, OutputLength, FeatureLength;
} HIDP_REPORT_ID;

typedef struct HIDP_PARSE {
    ULONG Collections;
    HIDP_COLLECTION *Collection;
    ULONG ReportIds;
    HIDP_REPORT_ID *ReportId;
    ULONG BreakOffset, ErrorCode; /* where and why a descriptor was refused */
} HIDP_PARSE;

NTSTATUS HidpParseDescriptor(const UCHAR *descriptor, ULONG length, HIDP_PARSE *out);
void HidpFreeParse(HIDP_PARSE *parse);
void IopInitializePads(void);
/* camera.c: the host's cameras, their devices and streams. */
void IopInitializeCameras(void);
void IopStartCameras(void);
NTSTATUS IopCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                       ULONG file_attributes, ULONG share, ULONG disposition, ULONG options, KPROCESSOR_MODE mode);
void IopPostCompletion(PFILE_OBJECT file, PVOID apc_context, NTSTATUS status, ULONG_PTR information, BOOLEAN synchronous);
void IopSignalFile(PFILE_OBJECT file);
NTSTATUS IopSetCompletion(PFILE_OBJECT file, HANDLE port, PVOID key);
void IopReleaseCompletion(PFILE_OBJECT file);
NTSTATUS IoQueryFileSize(PFILE_OBJECT file, ULONG64 *size);
NTSTATUS MimeFsDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry);

/* ------------------------------------------------------------------ ex */

extern POBJECT_TYPE ExEventType;
extern POBJECT_TYPE ExMutantType;
extern POBJECT_TYPE ExSemaphoreType;
extern POBJECT_TYPE ExTimerType;
extern POBJECT_TYPE MmSectionType;
extern KUSER_SHARED_DATA *SharedUserData;
extern ULONG64 KeBootFiletime;
/* ex/timezone.c: the offset of the clock follows the daylight time rules of the zone. */
void ExInitializeTimeZone(void);
void PsWin32TimeChange(void);
/* cm/boot.c: copies the rules of the machine's zone for a year into the key the kernel reads. */
void CmRefreshTimeZoneRules(ULONG year);

void ExInitialize(void);
void ExpInitializeAtoms(void);
void ExpInitializeWnf(void);
void ExUpdateSystemTime(void);
NTSTATUS ExWaitHandles(ULONG count, const HANDLE *handles, WAIT_TYPE type, BOOLEAN alertable,
                       PLARGE_INTEGER timeout, KPROCESSOR_MODE mode);

/* ------------------------------------------------------------------ mime session */

void MiSessionInitialize(void);
void MimeSessionInterrupt(ULONG bits);
void MimeSessionProcessExited(PEPROCESS process);
void MimeSendDebugOutput(PEPROCESS process, const char *text, SIZE_T length);
void MimeSendConsole(ULONG type, ULONG64 request, ULONG value, const void *data, ULONG length);

/* the console driver, io/console.c. */
void IopInitializeConsole(void);
MIME_CONSOLE *ConsoleCreate(ULONG64 request, ULONG run_flags, ULONG size);
void ConsoleDereference(MIME_CONSOLE *console);
NTSTATUS ConsoleAttach(PEPROCESS process, MIME_CONSOLE *console);
void ConsoleDetach(PEPROCESS process);
void ConsoleProcessExit(PEPROCESS process);
void ConsoleHostInput(ULONG64 request, const UCHAR *data, ULONG length);
void ConsoleHostEvent(ULONG64 request, ULONG event);
void ConsoleHostSize(ULONG64 request, ULONG size);
extern KEVENT MimeInputReadyEvent;
extern KEVENT MimeGpuEvent;

/* system services */
void KiInitializeSystemServices(void);

#endif
