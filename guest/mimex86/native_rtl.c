/*
 * ntdll's heap and synchronization code built into mimex86 under other
 * names. native.c runs the x64 ntdll exports with them: both builds share
 * the same structures and lock protocol, so x64 code that still runs its
 * own copy works on the same heaps and locks.
 */
#define RtlCreateHeap MxRtlCreateHeap
#define RtlDestroyHeap MxRtlDestroyHeap
#define RtlAllocateHeap MxRtlAllocateHeap
#define RtlFreeHeap MxRtlFreeHeap
#define RtlSizeHeap MxRtlSizeHeap
#define RtlReAllocateHeap MxRtlReAllocateHeap
#define RtlLockHeap MxRtlLockHeap
#define RtlUnlockHeap MxRtlUnlockHeap
#define RtlValidateHeap MxRtlValidateHeap
#define RtlCompactHeap MxRtlCompactHeap
#define RtlGetProcessHeaps MxRtlGetProcessHeaps
#define RtlSetHeapInformation MxRtlSetHeapInformation
#define RtlQueryHeapInformation MxRtlQueryHeapInformation
#define LdrpInitializeProcessHeap MxUnusedInitializeProcessHeap
#define MimeRtlAllocate MxUnusedRtlAllocate
#define MimeRtlFree MxUnusedRtlFree

#define RtlInitializeCriticalSectionEx MxRtlInitializeCriticalSectionEx
#define RtlInitializeCriticalSectionAndSpinCount MxRtlInitializeCriticalSectionAndSpinCount
#define RtlInitializeCriticalSection MxRtlInitializeCriticalSection
#define RtlSetCriticalSectionSpinCount MxRtlSetCriticalSectionSpinCount
#define RtlDeleteCriticalSection MxRtlDeleteCriticalSection
#define RtlEnterCriticalSection MxRtlEnterCriticalSection
#define RtlTryEnterCriticalSection MxRtlTryEnterCriticalSection
#define RtlLeaveCriticalSection MxRtlLeaveCriticalSection
#define RtlIsCriticalSectionLocked MxRtlIsCriticalSectionLocked
#define RtlIsCriticalSectionLockedByThread MxRtlIsCriticalSectionLockedByThread
#define RtlInitializeSRWLock MxRtlInitializeSRWLock
#define RtlTryAcquireSRWLockExclusive MxRtlTryAcquireSRWLockExclusive
#define RtlAcquireSRWLockExclusive MxRtlAcquireSRWLockExclusive
#define RtlReleaseSRWLockExclusive MxRtlReleaseSRWLockExclusive
#define RtlTryAcquireSRWLockShared MxRtlTryAcquireSRWLockShared
#define RtlAcquireSRWLockShared MxRtlAcquireSRWLockShared
#define RtlReleaseSRWLockShared MxRtlReleaseSRWLockShared
#define RtlInitializeConditionVariable MxRtlInitializeConditionVariable
#define RtlWakeConditionVariable MxRtlWakeConditionVariable
#define RtlWakeAllConditionVariable MxRtlWakeAllConditionVariable
#define RtlSleepConditionVariableCS MxRtlSleepConditionVariableCS
#define RtlSleepConditionVariableSRW MxRtlSleepConditionVariableSRW
#define RtlRunOnceInitialize MxRtlRunOnceInitialize
#define RtlRunOnceBeginInitialize MxRtlRunOnceBeginInitialize
#define RtlRunOnceComplete MxRtlRunOnceComplete
#define RtlRunOnceExecuteOnce MxUnusedRunOnceExecuteOnce
#define RtlWaitOnAddress MxRtlWaitOnAddress
#define RtlWakeAddressSingle MxRtlWakeAddressSingle
#define RtlWakeAddressAll MxRtlWakeAddressAll
#define RtlInitializeSListHead MxRtlInitializeSListHead
#define RtlInterlockedPushEntrySList MxRtlInterlockedPushEntrySList
#define RtlInterlockedPopEntrySList MxRtlInterlockedPopEntrySList
#define RtlInterlockedFlushSList MxRtlInterlockedFlushSList
#define RtlQueryDepthSList MxRtlQueryDepthSList
#define RtlFirstEntrySList MxRtlFirstEntrySList

/* raises in the x64 code that called the export, see main.c. */
#define RtlRaiseStatus MxRaiseStatus
#define DbgPrint MxDbgPrint

#include "../ntdll/heap.c"
#include "../ntdll/sync.c"

ULONG MxDbgPrint(PCSTR format, ...)
{
    return 0;
}
