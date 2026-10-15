/*
 * mime kernel internal interface.
 */
#ifndef MIME_NTOS_H
#define MIME_NTOS_H

#define MIME_KERNEL 1
#include <mime/abi.h>
#include <mime/ddk.h>
#include <mime/ntapi.h>
#include <stdbool.h>

#include "../rtl/rtl.h"

#define KI_TAG(a, b, c, d) ((ULONG)(a) | (ULONG)(b) << 8 | (ULONG)(c) << 16 | (ULONG)(d) << 24)

#if defined(__x86_64__)
#include "arch/x64/arch.h"
#else
#include "arch/arm64/arch.h"
#endif

__attribute__((returns_twice)) int KiSetJump(KJUMP_BUFFER *buffer);
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
void HalDisarmTimer(void);
#define HAL_IPI_INTID 1 /* the sgi asking a processor to reschedule */
ULONG64 HalCounterToHns(ULONG64 counter_delta);
ULONG64 HalHnsToCounter(ULONG64 hns);

void KiLogPrintf(const char *fmt, ...);
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
    ULONG64 ContextSwitches;
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
    LIST_ENTRY ThreadListHead;
    volatile LONG ActiveThreads;
} KPROCESS;

#define MAXIMUM_PROCESSORS 64

typedef struct _KPRCB {
    ULONG Number;
    ULONG Pad;
    struct _KTHREAD *CurrentThread;
    struct _KTHREAD *NextThread;
    struct _KTHREAD *IdleThread;
    ULONG64 TickTime; /* interrupt time of the last clock tick charged on this processor */
    volatile BOOLEAN DispatchPending;
    volatile BOOLEAN DpcPending;
    BOOLEAN InDpc;
    BOOLEAN Pad2;
    SINGLE_LIST_ENTRY DpcHead;
    PSINGLE_LIST_ENTRY *DpcTail;
    ULONG64 InterruptCount;
    ULONG64 ContextSwitches;
    ULONG64 DpcCount;
    ULONG64 IdleTicks;
    ULONG64 KernelTime, UserTime; /* charged with the threads' at each clock tick, in 100ns */
    ULONG64 IpiSent, IpiReceived;
    ULONG64 PendingSince; /* interrupt time an idle processor was asked to dispatch, 0 when not */
} KPRCB, *PKPRCB;

#if defined(__x86_64__)
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
extern volatile ULONG KiKernelLock[4]; /* ticket lock, owning processor, sleepers in the host */
#define KI_ASSERT_KERNEL_LOCK()                                                                                   \
    do {                                                                                                          \
        if (KiKernelLock[1] != KeGetCurrentPrcb()->Number)                                                        \
            KeBugCheckEx(0xDEAD10C, KiKernelLock[1], KeGetCurrentPrcb()->Number, __LINE__, 0);                    \
    } while (0)
extern ULONG64 KiKernelLockPhys;
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

void KiInitializeKernel(MimeBootInfo *boot);
void KiInitializeProcessor(PKPCR pcr);
void KiStartProcessors(void);
void KiSecondaryStartup(void);
void KiRequestDispatch(ULONG processor);
void KiInitializeScheduler(void);
void KiInitializeThread(PKTHREAD thread, PKPROCESS process, PVOID stack_top, PVOID stack_limit,
                        PKSTART_ROUTINE routine, PVOID context);
void KiReadyThread(PKTHREAD thread);
void KiInsertReadyThread(PKTHREAD thread);
void KiInterruptThread(PKTHREAD thread);
void KiStartThread(PKTHREAD thread);
void KiSwapThread(void);
void KiDispatchPendingWork(void);
void KiDeliverDpcs(void);
void KiCheckPreemption(void);
NTSTATUS KiWaitObjects(ULONG count, PVOID objects[], WAIT_TYPE type, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                       BOOLEAN alertable, PLARGE_INTEGER timeout, PKWAIT_BLOCK blocks);
void KiSignalWaiters(PDISPATCHER_HEADER object, KPRIORITY increment);
void KiUnwaitThread(PKTHREAD thread, LONG_PTR status);
void KiInitializeTimers(void);
void KiClockInterrupt(PKTRAP_FRAME frame);
ULONG64 KiQueryInterruptTime(void);
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

/* arch/<processor>/cpu.c, the identification of the boot processor. */
void KiReadProcessorIds(void);
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

typedef struct _MMVAD {
    LIST_ENTRY Links;
    ULONG_PTR StartVa;
    ULONG_PTR EndVa; /* exclusive */
    ULONG Type;
    ULONG InitialProtect;
    struct _SECTION *Section;
    ULONG64 SectionOffset;
} MMVAD, *PMMVAD;

typedef struct _MMADDRESS_SPACE {
    LIST_ENTRY VadList;
    PMMVAD Hint;
    FAST_MUTEX Lock;
    ULONG64 *Root; /* physmap pointer to the l0 table */
    ULONG64 RootPhys;
    USHORT Asid;
    SIZE_T CommittedPages;
    ULONG_PTR AllocationLimit;
    ULONG_PTR LastFaultEnd; /* end of the last demand zero window, to spot sequential touching */
} MMADDRESS_SPACE, *PMMADDRESS_SPACE;

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
} SECTION, *PSECTION;

ULONG64 MiSectionPage(PSECTION s, ULONG index);
ULONG64 MiSectionSwPte(ULONG protect);
ULONG64 MiSectionPteAttrs(ULONG protect, ULONG64 pa, BOOLEAN shared);

void MmInitialize(MimeBootInfo *boot);
void MmInitializePhase1(void);
ULONG64 MmAllocatePage(void);           /* zeroed, returns physical address, 0 on failure */
ULONG64 MmAllocatePageNoZero(void);
void MmFreePage(ULONG64 pa);
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
BOOLEAN MiRangeIsFree(PMMADDRESS_SPACE as, ULONG_PTR start, ULONG_PTR end);
void MiRemoveVad(PMMADDRESS_SPACE as, PMMVAD vad);
NTSTATUS MiAllocateVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type, ULONG protect);
NTSTATUS MiFreeVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG type);
NTSTATUS MiProtectVirtual(PEPROCESS process, PVOID *base, SIZE_T *size, ULONG protect, PULONG old);
NTSTATUS MiQueryVirtual(PEPROCESS process, PVOID base, PMEMORY_BASIC_INFORMATION info);
BOOLEAN MmAccessFault(ULONG_PTR va, BOOLEAN write, BOOLEAN execute, KPROCESSOR_MODE mode);
NTSTATUS MiCreateImageSection(PFILE_OBJECT file, PSECTION *out);
NTSTATUS MiCreateDataSection(PFILE_OBJECT file, ULONG64 size, ULONG protect, ULONG flags, PSECTION *out);
NTSTATUS MiMapViewOfSection(PSECTION section, PEPROCESS process, PVOID *base, ULONG64 offset, SIZE_T *view_size,
                            ULONG protect, ULONG allocation_type);
NTSTATUS MiUnmapViewOfSection(PEPROCESS process, PVOID base);
NTSTATUS MiParseImageHeaders(const void *headers, SIZE_T length, MI_IMAGE_INFO *info);
NTSTATUS MmLoadSystemImage(PCUNICODE_STRING path, PVOID *base, SIZE_T *size, PVOID *entry);
PVOID MiFindKernelExport(PVOID image_base, const char *name);
NTSTATUS MiReadFileBytes(PFILE_OBJECT file, ULONG64 offset, PVOID buffer, ULONG length, PULONG read);

/* ------------------------------------------------------------------ ob */

typedef struct _OBJECT_TYPE_INITIALIZER {
    ULONG PoolType;
    ACCESS_MASK ValidAccessMask;
    ULONG DefaultObjectSize;
    BOOLEAN Waitable; /* the object starts with a DISPATCHER_HEADER */
    void (*CloseProcedure)(PEPROCESS process, PVOID object, ULONG handle_count);
    void (*DeleteProcedure)(PVOID object);
    NTSTATUS (*ParseProcedure)(PVOID object, PVOID type, ULONG attributes, PUNICODE_STRING complete_name,
                               PUNICODE_STRING remaining, PVOID context, PVOID *found);
    PVOID (*WaitObject)(PVOID object); /* dispatcher object to wait on, if not the body itself */
} OBJECT_TYPE_INITIALIZER;

typedef struct _OBJECT_TYPE {
    UNICODE_STRING Name;
    WCHAR NameBuffer[32];
    OBJECT_TYPE_INITIALIZER Init;
    ULONG Index;
    volatile LONG TotalObjects;
    volatile LONG TotalHandles;
} OBJECT_TYPE;

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
POBJECT_TYPE ObCreateObjectType(PCWSTR name, const OBJECT_TYPE_INITIALIZER *init);
NTSTATUS ObCreateObject(POBJECT_TYPE type, POBJECT_ATTRIBUTES attributes, KPROCESSOR_MODE mode, ULONG body_size,
                        PVOID *object);
NTSTATUS ObInsertObject(PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle);
NTSTATUS ObInsertObjectNamed(PVOID object, POBJECT_ATTRIBUTES attributes, ACCESS_MASK access, KPROCESSOR_MODE mode,
                             PHANDLE handle);
NTSTATUS ObCreateHandle(PEPROCESS process, PVOID object, ACCESS_MASK access, ULONG attributes, PHANDLE handle);
NTSTATUS ObInheritHandles(PEPROCESS parent, PEPROCESS child);
NTSTATUS ObCloseHandle(HANDLE handle, KPROCESSOR_MODE mode);
NTSTATUS ObReferenceHandle(HANDLE handle, POBJECT_TYPE type, KPROCESSOR_MODE mode, PVOID *object,
                           ACCESS_MASK *granted);
NTSTATUS ObLookupObjectName(PUNICODE_STRING name, HANDLE root, ULONG attributes, POBJECT_TYPE type,
                            KPROCESSOR_MODE mode, PVOID parse_context, PVOID *object);
NTSTATUS ObOpenObjectByName(POBJECT_ATTRIBUTES attributes, POBJECT_TYPE type, KPROCESSOR_MODE mode,
                            ACCESS_MASK access, PVOID parse_context, PHANDLE handle);
NTSTATUS ObInsertIntoDirectory(POBJECT_DIRECTORY dir, PVOID object, PCUNICODE_STRING name);
NTSTATUS ObCreateDirectory(PCWSTR path, POBJECT_DIRECTORY *out);
NTSTATUS ObCreateSymbolicLink(PCWSTR link, PCWSTR target);
NTSTATUS ObInitializeHandleTable(PHANDLE_TABLE table);
void ObKillProcessHandles(PEPROCESS process);
NTSTATUS ObDuplicateHandle(PEPROCESS source_process, HANDLE source, PEPROCESS target_process, PHANDLE target,
                           ACCESS_MASK access, ULONG attributes, ULONG options);
PDISPATCHER_HEADER ObGetWaitObject(PVOID object);
NTSTATUS ObCaptureObjectAttributes(POBJECT_ATTRIBUTES user, POBJECT_ATTRIBUTES copy, PUNICODE_STRING name);
NTSTATUS ObStoreHandle(PHANDLE user_handle, HANDLE handle);
NTSTATUS ObCreateSymbolicLinkObject(PCUNICODE_STRING link, PCUNICODE_STRING target, BOOLEAN permanent);
void ObMakeTemporaryObject(PVOID object);
POBJECT_TYPE ObGetObjectType(PVOID object);
NTSTATUS ObQueryNameString(PVOID object, PUNICODE_STRING name, PWSTR buffer, ULONG buffer_chars);

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
    PVOID NativeStackAllocation; /* the translator's own stack in x86 and x64 processes */
    SIZE_T NativeStackSize;
    ULONG64 CreateTime;
    ULONG64 ExitTime;
    LONG_PTR SameThreadApcFlags;
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
    ULONG64 CreateTime;
    ULONG64 ExitTime;
    KEVENT ExitEvent;
    struct EJOB *Job; /* ps/job.c */
    LIST_ENTRY JobLinks;
} EPROCESS;

extern LIST_ENTRY PsActiveProcessHead;
extern PEPROCESS PsIdleProcess;
extern POBJECT_TYPE PsProcessTypeObject;
extern POBJECT_TYPE PsThreadTypeObject;

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
                             PEPROCESS *out);
DECLSPEC_NORETURN void PspExitThread(NTSTATUS status);
NTSTATUS PspTerminateProcess(PEPROCESS process, NTSTATUS status);
#define PROCESS_CREATE_FLAGS_BREAKAWAY 0x00000001
void PspInitializeJobs(void);
void PspInheritJob(PEPROCESS process, PEPROCESS parent, ULONG flags);
void PspJobProcessExit(PEPROCESS process);
void PsDumpThreads(void);
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

/* ------------------------------------------------------------------ io */

extern POBJECT_TYPE IoDeviceObjectType;
extern POBJECT_TYPE IoDriverObjectType;
extern POBJECT_TYPE IoFileObjectTypeObject;

void IoInitialize(void);
NTSTATUS IoCreateDriver(PCWSTR name, PDRIVER_INITIALIZE init, PDRIVER_OBJECT *out);
NTSTATUS IopCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb,
                       ULONG file_attributes, ULONG share, ULONG disposition, ULONG options, KPROCESSOR_MODE mode);
NTSTATUS IopOpenFileObject(PCUNICODE_STRING path, ACCESS_MASK access, ULONG options, PFILE_OBJECT *out);
NTSTATUS IopSynchronousRequest(PFILE_OBJECT file, UCHAR major, UCHAR minor, PVOID buffer, ULONG length,
                               PIO_STACK_LOCATION params, PIO_STATUS_BLOCK iosb);
const MimeRomEntry *IoGetRomFile(PFILE_OBJECT file);
BOOLEAN IoGetFileWindow(PFILE_OBJECT file, PULONG64 pa, PULONG64 size, PVOID *ref);
void IoReleaseFileWindow(PVOID ref);

NTSTATUS PspBuildEnvironment(USHORT machine, PWCHAR *block, SIZE_T *bytes);

/* cm, the configuration manager. */
extern POBJECT_TYPE CmKeyType;
void CmInitSystem1(void);
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
NTSTATUS IoReadFileObject(PFILE_OBJECT file, PVOID buffer, ULONG length, ULONG64 offset, PULONG read);
void IopInitializeCompletion(void);
void IopInitializeNamedPipes(void);
void IopQueueCompletionApc(PETHREAD thread, PVOID routine, PVOID context, PIO_STATUS_BLOCK iosb);
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

void ExInitialize(void);
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
