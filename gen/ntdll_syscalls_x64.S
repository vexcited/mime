/* generated, do not edit. */
    .text
    .globl NtAccessCheck
    .globl ZwAccessCheck
    .p2align 5
NtAccessCheck:
ZwAccessCheck:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWorkerFactoryWorkerReady
    .globl ZwWorkerFactoryWorkerReady
    .p2align 5
NtWorkerFactoryWorkerReady:
ZwWorkerFactoryWorkerReady:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAcceptConnectPort
    .globl ZwAcceptConnectPort
    .p2align 5
NtAcceptConnectPort:
ZwAcceptConnectPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMapUserPhysicalPagesScatter
    .globl ZwMapUserPhysicalPagesScatter
    .p2align 5
NtMapUserPhysicalPagesScatter:
ZwMapUserPhysicalPagesScatter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitForSingleObject
    .globl ZwWaitForSingleObject
    .p2align 5
NtWaitForSingleObject:
ZwWaitForSingleObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCallbackReturn
    .globl ZwCallbackReturn
    .p2align 5
NtCallbackReturn:
ZwCallbackReturn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReadFile
    .globl ZwReadFile
    .p2align 5
NtReadFile:
ZwReadFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeviceIoControlFile
    .globl ZwDeviceIoControlFile
    .p2align 5
NtDeviceIoControlFile:
ZwDeviceIoControlFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWriteFile
    .globl ZwWriteFile
    .p2align 5
NtWriteFile:
ZwWriteFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRemoveIoCompletion
    .globl ZwRemoveIoCompletion
    .p2align 5
NtRemoveIoCompletion:
ZwRemoveIoCompletion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReleaseSemaphore
    .globl ZwReleaseSemaphore
    .p2align 5
NtReleaseSemaphore:
ZwReleaseSemaphore:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReplyWaitReceivePort
    .globl ZwReplyWaitReceivePort
    .p2align 5
NtReplyWaitReceivePort:
ZwReplyWaitReceivePort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReplyPort
    .globl ZwReplyPort
    .p2align 5
NtReplyPort:
ZwReplyPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationThread
    .globl ZwSetInformationThread
    .p2align 5
NtSetInformationThread:
ZwSetInformationThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetEvent
    .globl ZwSetEvent
    .p2align 5
NtSetEvent:
ZwSetEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtClose
    .globl ZwClose
    .p2align 5
NtClose:
ZwClose:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryObject
    .globl ZwQueryObject
    .p2align 5
NtQueryObject:
ZwQueryObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationFile
    .globl ZwQueryInformationFile
    .p2align 5
NtQueryInformationFile:
ZwQueryInformationFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenKey
    .globl ZwOpenKey
    .p2align 5
NtOpenKey:
ZwOpenKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnumerateValueKey
    .globl ZwEnumerateValueKey
    .p2align 5
NtEnumerateValueKey:
ZwEnumerateValueKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFindAtom
    .globl ZwFindAtom
    .p2align 5
NtFindAtom:
ZwFindAtom:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryDefaultLocale
    .globl ZwQueryDefaultLocale
    .p2align 5
NtQueryDefaultLocale:
ZwQueryDefaultLocale:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryKey
    .globl ZwQueryKey
    .p2align 5
NtQueryKey:
ZwQueryKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x16, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryValueKey
    .globl ZwQueryValueKey
    .p2align 5
NtQueryValueKey:
ZwQueryValueKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x17, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAllocateVirtualMemory
    .globl ZwAllocateVirtualMemory
    .p2align 5
NtAllocateVirtualMemory:
ZwAllocateVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x18, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationProcess
    .globl ZwQueryInformationProcess
    .p2align 5
NtQueryInformationProcess:
ZwQueryInformationProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x19, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitForMultipleObjects32
    .globl ZwWaitForMultipleObjects32
    .p2align 5
NtWaitForMultipleObjects32:
ZwWaitForMultipleObjects32:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWriteFileGather
    .globl ZwWriteFileGather
    .p2align 5
NtWriteFileGather:
ZwWriteFileGather:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationProcess
    .globl ZwSetInformationProcess
    .p2align 5
NtSetInformationProcess:
ZwSetInformationProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateKey
    .globl ZwCreateKey
    .p2align 5
NtCreateKey:
ZwCreateKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFreeVirtualMemory
    .globl ZwFreeVirtualMemory
    .p2align 5
NtFreeVirtualMemory:
ZwFreeVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtImpersonateClientOfPort
    .globl ZwImpersonateClientOfPort
    .p2align 5
NtImpersonateClientOfPort:
ZwImpersonateClientOfPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReleaseMutant
    .globl ZwReleaseMutant
    .p2align 5
NtReleaseMutant:
ZwReleaseMutant:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x20, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationToken
    .globl ZwQueryInformationToken
    .p2align 5
NtQueryInformationToken:
ZwQueryInformationToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x21, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRequestWaitReplyPort
    .globl ZwRequestWaitReplyPort
    .p2align 5
NtRequestWaitReplyPort:
ZwRequestWaitReplyPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x22, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryVirtualMemory
    .globl ZwQueryVirtualMemory
    .p2align 5
NtQueryVirtualMemory:
ZwQueryVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x23, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenThreadToken
    .globl ZwOpenThreadToken
    .p2align 5
NtOpenThreadToken:
ZwOpenThreadToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x24, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationThread
    .globl ZwQueryInformationThread
    .p2align 5
NtQueryInformationThread:
ZwQueryInformationThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x25, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenProcess
    .globl ZwOpenProcess
    .p2align 5
NtOpenProcess:
ZwOpenProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x26, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationFile
    .globl ZwSetInformationFile
    .p2align 5
NtSetInformationFile:
ZwSetInformationFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x27, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMapViewOfSection
    .globl ZwMapViewOfSection
    .p2align 5
NtMapViewOfSection:
ZwMapViewOfSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x28, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAccessCheckAndAuditAlarm
    .globl ZwAccessCheckAndAuditAlarm
    .p2align 5
NtAccessCheckAndAuditAlarm:
ZwAccessCheckAndAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x29, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnmapViewOfSection
    .globl ZwUnmapViewOfSection
    .p2align 5
NtUnmapViewOfSection:
ZwUnmapViewOfSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x2A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReplyWaitReceivePortEx
    .globl ZwReplyWaitReceivePortEx
    .p2align 5
NtReplyWaitReceivePortEx:
ZwReplyWaitReceivePortEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x2B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTerminateProcess
    .globl ZwTerminateProcess
    .p2align 5
NtTerminateProcess:
ZwTerminateProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x2C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetEventBoostPriority
    .globl ZwSetEventBoostPriority
    .p2align 5
NtSetEventBoostPriority:
ZwSetEventBoostPriority:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x2D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReadFileScatter
    .globl ZwReadFileScatter
    .p2align 5
NtReadFileScatter:
ZwReadFileScatter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x2E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenThreadTokenEx
    .globl ZwOpenThreadTokenEx
    .p2align 5
NtOpenThreadTokenEx:
ZwOpenThreadTokenEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x2F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenProcessTokenEx
    .globl ZwOpenProcessTokenEx
    .p2align 5
NtOpenProcessTokenEx:
ZwOpenProcessTokenEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x30, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryPerformanceCounter
    .globl ZwQueryPerformanceCounter
    .p2align 5
NtQueryPerformanceCounter:
ZwQueryPerformanceCounter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x31, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnumerateKey
    .globl ZwEnumerateKey
    .p2align 5
NtEnumerateKey:
ZwEnumerateKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x32, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenFile
    .globl ZwOpenFile
    .p2align 5
NtOpenFile:
ZwOpenFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x33, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDelayExecution
    .globl ZwDelayExecution
    .p2align 5
NtDelayExecution:
ZwDelayExecution:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x34, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryDirectoryFile
    .globl ZwQueryDirectoryFile
    .p2align 5
NtQueryDirectoryFile:
ZwQueryDirectoryFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x35, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySystemInformation
    .globl ZwQuerySystemInformation
    .p2align 5
NtQuerySystemInformation:
ZwQuerySystemInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x36, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl RtlGetNativeSystemInformation
    .p2align 5
RtlGetNativeSystemInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x36, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenSection
    .globl ZwOpenSection
    .p2align 5
NtOpenSection:
ZwOpenSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x37, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryTimer
    .globl ZwQueryTimer
    .p2align 5
NtQueryTimer:
ZwQueryTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x38, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFsControlFile
    .globl ZwFsControlFile
    .p2align 5
NtFsControlFile:
ZwFsControlFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x39, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWriteVirtualMemory
    .globl ZwWriteVirtualMemory
    .p2align 5
NtWriteVirtualMemory:
ZwWriteVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x3A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCloseObjectAuditAlarm
    .globl ZwCloseObjectAuditAlarm
    .p2align 5
NtCloseObjectAuditAlarm:
ZwCloseObjectAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x3B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDuplicateObject
    .globl ZwDuplicateObject
    .p2align 5
NtDuplicateObject:
ZwDuplicateObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x3C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryAttributesFile
    .globl ZwQueryAttributesFile
    .p2align 5
NtQueryAttributesFile:
ZwQueryAttributesFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x3D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtClearEvent
    .globl ZwClearEvent
    .p2align 5
NtClearEvent:
ZwClearEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x3E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReadVirtualMemory
    .globl ZwReadVirtualMemory
    .p2align 5
NtReadVirtualMemory:
ZwReadVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x3F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenEvent
    .globl ZwOpenEvent
    .p2align 5
NtOpenEvent:
ZwOpenEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x40, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAdjustPrivilegesToken
    .globl ZwAdjustPrivilegesToken
    .p2align 5
NtAdjustPrivilegesToken:
ZwAdjustPrivilegesToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x41, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDuplicateToken
    .globl ZwDuplicateToken
    .p2align 5
NtDuplicateToken:
ZwDuplicateToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x42, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtContinue
    .globl ZwContinue
    .p2align 5
NtContinue:
ZwContinue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x43, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryDefaultUILanguage
    .globl ZwQueryDefaultUILanguage
    .p2align 5
NtQueryDefaultUILanguage:
ZwQueryDefaultUILanguage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x44, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueueApcThread
    .globl ZwQueueApcThread
    .p2align 5
NtQueueApcThread:
ZwQueueApcThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x45, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtYieldExecution
    .globl ZwYieldExecution
    .p2align 5
NtYieldExecution:
ZwYieldExecution:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x46, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAddAtom
    .globl ZwAddAtom
    .p2align 5
NtAddAtom:
ZwAddAtom:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x47, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateEvent
    .globl ZwCreateEvent
    .p2align 5
NtCreateEvent:
ZwCreateEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x48, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryVolumeInformationFile
    .globl ZwQueryVolumeInformationFile
    .p2align 5
NtQueryVolumeInformationFile:
ZwQueryVolumeInformationFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x49, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateSection
    .globl ZwCreateSection
    .p2align 5
NtCreateSection:
ZwCreateSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x4A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushBuffersFile
    .globl ZwFlushBuffersFile
    .p2align 5
NtFlushBuffersFile:
ZwFlushBuffersFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x4B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtApphelpCacheControl
    .globl ZwApphelpCacheControl
    .p2align 5
NtApphelpCacheControl:
ZwApphelpCacheControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x4C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateProcessEx
    .globl ZwCreateProcessEx
    .p2align 5
NtCreateProcessEx:
ZwCreateProcessEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x4D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateThread
    .globl ZwCreateThread
    .p2align 5
NtCreateThread:
ZwCreateThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x4E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtIsProcessInJob
    .globl ZwIsProcessInJob
    .p2align 5
NtIsProcessInJob:
ZwIsProcessInJob:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x4F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtProtectVirtualMemory
    .globl ZwProtectVirtualMemory
    .p2align 5
NtProtectVirtualMemory:
ZwProtectVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x50, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySection
    .globl ZwQuerySection
    .p2align 5
NtQuerySection:
ZwQuerySection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x51, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtResumeThread
    .globl ZwResumeThread
    .p2align 5
NtResumeThread:
ZwResumeThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x52, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTerminateThread
    .globl ZwTerminateThread
    .p2align 5
NtTerminateThread:
ZwTerminateThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x53, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReadRequestData
    .globl ZwReadRequestData
    .p2align 5
NtReadRequestData:
ZwReadRequestData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x54, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateFile
    .globl ZwCreateFile
    .p2align 5
NtCreateFile:
ZwCreateFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x55, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryEvent
    .globl ZwQueryEvent
    .p2align 5
NtQueryEvent:
ZwQueryEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x56, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWriteRequestData
    .globl ZwWriteRequestData
    .p2align 5
NtWriteRequestData:
ZwWriteRequestData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x57, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenDirectoryObject
    .globl ZwOpenDirectoryObject
    .p2align 5
NtOpenDirectoryObject:
ZwOpenDirectoryObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x58, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAccessCheckByTypeAndAuditAlarm
    .globl ZwAccessCheckByTypeAndAuditAlarm
    .p2align 5
NtAccessCheckByTypeAndAuditAlarm:
ZwAccessCheckByTypeAndAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x59, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySystemTime
    .globl ZwQuerySystemTime
    .p2align 5
NtQuerySystemTime:
ZwQuerySystemTime:
    jmp RtlQuerySystemTime
    .globl NtWaitForMultipleObjects
    .globl ZwWaitForMultipleObjects
    .p2align 5
NtWaitForMultipleObjects:
ZwWaitForMultipleObjects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x5B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationObject
    .globl ZwSetInformationObject
    .p2align 5
NtSetInformationObject:
ZwSetInformationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x5C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCancelIoFile
    .globl ZwCancelIoFile
    .p2align 5
NtCancelIoFile:
ZwCancelIoFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x5D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTraceEvent
    .globl ZwTraceEvent
    .p2align 5
NtTraceEvent:
ZwTraceEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x5E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPowerInformation
    .globl ZwPowerInformation
    .p2align 5
NtPowerInformation:
ZwPowerInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x5F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetValueKey
    .globl ZwSetValueKey
    .p2align 5
NtSetValueKey:
ZwSetValueKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x60, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCancelTimer
    .globl ZwCancelTimer
    .p2align 5
NtCancelTimer:
ZwCancelTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x61, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetTimer
    .globl ZwSetTimer
    .p2align 5
NtSetTimer:
ZwSetTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x62, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAccessCheckByType
    .globl ZwAccessCheckByType
    .p2align 5
NtAccessCheckByType:
ZwAccessCheckByType:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x63, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAccessCheckByTypeResultList
    .globl ZwAccessCheckByTypeResultList
    .p2align 5
NtAccessCheckByTypeResultList:
ZwAccessCheckByTypeResultList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x64, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAccessCheckByTypeResultListAndAuditAlarm
    .globl ZwAccessCheckByTypeResultListAndAuditAlarm
    .p2align 5
NtAccessCheckByTypeResultListAndAuditAlarm:
ZwAccessCheckByTypeResultListAndAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x65, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAccessCheckByTypeResultListAndAuditAlarmByHandle
    .globl ZwAccessCheckByTypeResultListAndAuditAlarmByHandle
    .p2align 5
NtAccessCheckByTypeResultListAndAuditAlarmByHandle:
ZwAccessCheckByTypeResultListAndAuditAlarmByHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x66, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAcquireCrossVmMutant
    .globl ZwAcquireCrossVmMutant
    .p2align 5
NtAcquireCrossVmMutant:
ZwAcquireCrossVmMutant:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x67, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAcquireProcessActivityReference
    .globl ZwAcquireProcessActivityReference
    .p2align 5
NtAcquireProcessActivityReference:
ZwAcquireProcessActivityReference:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x68, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAddAtomEx
    .globl ZwAddAtomEx
    .p2align 5
NtAddAtomEx:
ZwAddAtomEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x69, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAddBootEntry
    .globl ZwAddBootEntry
    .p2align 5
NtAddBootEntry:
ZwAddBootEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x6A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAddDriverEntry
    .globl ZwAddDriverEntry
    .p2align 5
NtAddDriverEntry:
ZwAddDriverEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x6B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAdjustGroupsToken
    .globl ZwAdjustGroupsToken
    .p2align 5
NtAdjustGroupsToken:
ZwAdjustGroupsToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x6C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAdjustTokenClaimsAndDeviceGroups
    .globl ZwAdjustTokenClaimsAndDeviceGroups
    .p2align 5
NtAdjustTokenClaimsAndDeviceGroups:
ZwAdjustTokenClaimsAndDeviceGroups:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x6D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlertMultipleThreadByThreadId
    .globl ZwAlertMultipleThreadByThreadId
    .p2align 5
NtAlertMultipleThreadByThreadId:
ZwAlertMultipleThreadByThreadId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x6E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlertResumeThread
    .globl ZwAlertResumeThread
    .p2align 5
NtAlertResumeThread:
ZwAlertResumeThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x6F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlertThread
    .globl ZwAlertThread
    .p2align 5
NtAlertThread:
ZwAlertThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x70, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlertThreadByThreadId
    .globl ZwAlertThreadByThreadId
    .p2align 5
NtAlertThreadByThreadId:
ZwAlertThreadByThreadId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x71, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlertThreadByThreadIdEx
    .globl ZwAlertThreadByThreadIdEx
    .p2align 5
NtAlertThreadByThreadIdEx:
ZwAlertThreadByThreadIdEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x72, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAllocateLocallyUniqueId
    .globl ZwAllocateLocallyUniqueId
    .p2align 5
NtAllocateLocallyUniqueId:
ZwAllocateLocallyUniqueId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x73, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAllocateReserveObject
    .globl ZwAllocateReserveObject
    .p2align 5
NtAllocateReserveObject:
ZwAllocateReserveObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x74, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAllocateUserPhysicalPages
    .globl ZwAllocateUserPhysicalPages
    .p2align 5
NtAllocateUserPhysicalPages:
ZwAllocateUserPhysicalPages:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x75, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAllocateUserPhysicalPagesEx
    .globl ZwAllocateUserPhysicalPagesEx
    .p2align 5
NtAllocateUserPhysicalPagesEx:
ZwAllocateUserPhysicalPagesEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x76, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAllocateUuids
    .globl ZwAllocateUuids
    .p2align 5
NtAllocateUuids:
ZwAllocateUuids:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x77, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAllocateVirtualMemoryEx
    .globl ZwAllocateVirtualMemoryEx
    .p2align 5
NtAllocateVirtualMemoryEx:
ZwAllocateVirtualMemoryEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x78, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcAcceptConnectPort
    .globl ZwAlpcAcceptConnectPort
    .p2align 5
NtAlpcAcceptConnectPort:
ZwAlpcAcceptConnectPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x79, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcCancelMessage
    .globl ZwAlpcCancelMessage
    .p2align 5
NtAlpcCancelMessage:
ZwAlpcCancelMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x7A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcConnectPort
    .globl ZwAlpcConnectPort
    .p2align 5
NtAlpcConnectPort:
ZwAlpcConnectPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x7B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcConnectPortEx
    .globl ZwAlpcConnectPortEx
    .p2align 5
NtAlpcConnectPortEx:
ZwAlpcConnectPortEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x7C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcCreatePort
    .globl ZwAlpcCreatePort
    .p2align 5
NtAlpcCreatePort:
ZwAlpcCreatePort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x7D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcCreatePortSection
    .globl ZwAlpcCreatePortSection
    .p2align 5
NtAlpcCreatePortSection:
ZwAlpcCreatePortSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x7E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcCreateResourceReserve
    .globl ZwAlpcCreateResourceReserve
    .p2align 5
NtAlpcCreateResourceReserve:
ZwAlpcCreateResourceReserve:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x7F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcCreateSectionView
    .globl ZwAlpcCreateSectionView
    .p2align 5
NtAlpcCreateSectionView:
ZwAlpcCreateSectionView:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x80, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcCreateSecurityContext
    .globl ZwAlpcCreateSecurityContext
    .p2align 5
NtAlpcCreateSecurityContext:
ZwAlpcCreateSecurityContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x81, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcDeletePortSection
    .globl ZwAlpcDeletePortSection
    .p2align 5
NtAlpcDeletePortSection:
ZwAlpcDeletePortSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x82, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcDeleteResourceReserve
    .globl ZwAlpcDeleteResourceReserve
    .p2align 5
NtAlpcDeleteResourceReserve:
ZwAlpcDeleteResourceReserve:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x83, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcDeleteSectionView
    .globl ZwAlpcDeleteSectionView
    .p2align 5
NtAlpcDeleteSectionView:
ZwAlpcDeleteSectionView:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x84, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcDeleteSecurityContext
    .globl ZwAlpcDeleteSecurityContext
    .p2align 5
NtAlpcDeleteSecurityContext:
ZwAlpcDeleteSecurityContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x85, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcDisconnectPort
    .globl ZwAlpcDisconnectPort
    .p2align 5
NtAlpcDisconnectPort:
ZwAlpcDisconnectPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x86, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcImpersonateClientContainerOfPort
    .globl ZwAlpcImpersonateClientContainerOfPort
    .p2align 5
NtAlpcImpersonateClientContainerOfPort:
ZwAlpcImpersonateClientContainerOfPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x87, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcImpersonateClientOfPort
    .globl ZwAlpcImpersonateClientOfPort
    .p2align 5
NtAlpcImpersonateClientOfPort:
ZwAlpcImpersonateClientOfPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x88, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcOpenSenderProcess
    .globl ZwAlpcOpenSenderProcess
    .p2align 5
NtAlpcOpenSenderProcess:
ZwAlpcOpenSenderProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x89, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcOpenSenderThread
    .globl ZwAlpcOpenSenderThread
    .p2align 5
NtAlpcOpenSenderThread:
ZwAlpcOpenSenderThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x8A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcQueryInformation
    .globl ZwAlpcQueryInformation
    .p2align 5
NtAlpcQueryInformation:
ZwAlpcQueryInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x8B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcQueryInformationMessage
    .globl ZwAlpcQueryInformationMessage
    .p2align 5
NtAlpcQueryInformationMessage:
ZwAlpcQueryInformationMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x8C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcRevokeSecurityContext
    .globl ZwAlpcRevokeSecurityContext
    .p2align 5
NtAlpcRevokeSecurityContext:
ZwAlpcRevokeSecurityContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x8D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcSendWaitReceivePort
    .globl ZwAlpcSendWaitReceivePort
    .p2align 5
NtAlpcSendWaitReceivePort:
ZwAlpcSendWaitReceivePort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x8E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAlpcSetInformation
    .globl ZwAlpcSetInformation
    .p2align 5
NtAlpcSetInformation:
ZwAlpcSetInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x8F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAreMappedFilesTheSame
    .globl ZwAreMappedFilesTheSame
    .p2align 5
NtAreMappedFilesTheSame:
ZwAreMappedFilesTheSame:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x90, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAssignProcessToJobObject
    .globl ZwAssignProcessToJobObject
    .p2align 5
NtAssignProcessToJobObject:
ZwAssignProcessToJobObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x91, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtAssociateWaitCompletionPacket
    .globl ZwAssociateWaitCompletionPacket
    .p2align 5
NtAssociateWaitCompletionPacket:
ZwAssociateWaitCompletionPacket:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x92, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCallEnclave
    .globl ZwCallEnclave
    .p2align 5
NtCallEnclave:
ZwCallEnclave:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x93, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCancelIoFileEx
    .globl ZwCancelIoFileEx
    .p2align 5
NtCancelIoFileEx:
ZwCancelIoFileEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x94, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCancelSynchronousIoFile
    .globl ZwCancelSynchronousIoFile
    .p2align 5
NtCancelSynchronousIoFile:
ZwCancelSynchronousIoFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x95, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCancelTimer2
    .globl ZwCancelTimer2
    .p2align 5
NtCancelTimer2:
ZwCancelTimer2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x96, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCancelWaitCompletionPacket
    .globl ZwCancelWaitCompletionPacket
    .p2align 5
NtCancelWaitCompletionPacket:
ZwCancelWaitCompletionPacket:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x97, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtChangeProcessState
    .globl ZwChangeProcessState
    .p2align 5
NtChangeProcessState:
ZwChangeProcessState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x98, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtChangeThreadState
    .globl ZwChangeThreadState
    .p2align 5
NtChangeThreadState:
ZwChangeThreadState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x99, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCommitComplete
    .globl ZwCommitComplete
    .p2align 5
NtCommitComplete:
ZwCommitComplete:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x9A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCommitEnlistment
    .globl ZwCommitEnlistment
    .p2align 5
NtCommitEnlistment:
ZwCommitEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x9B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCommitRegistryTransaction
    .globl ZwCommitRegistryTransaction
    .p2align 5
NtCommitRegistryTransaction:
ZwCommitRegistryTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x9C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCommitTransaction
    .globl ZwCommitTransaction
    .p2align 5
NtCommitTransaction:
ZwCommitTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x9D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompactKeys
    .globl ZwCompactKeys
    .p2align 5
NtCompactKeys:
ZwCompactKeys:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x9E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompareObjects
    .globl ZwCompareObjects
    .p2align 5
NtCompareObjects:
ZwCompareObjects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x9F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompareSigningLevels
    .globl ZwCompareSigningLevels
    .p2align 5
NtCompareSigningLevels:
ZwCompareSigningLevels:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompareTokens
    .globl ZwCompareTokens
    .p2align 5
NtCompareTokens:
ZwCompareTokens:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompleteConnectPort
    .globl ZwCompleteConnectPort
    .p2align 5
NtCompleteConnectPort:
ZwCompleteConnectPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompressKey
    .globl ZwCompressKey
    .p2align 5
NtCompressKey:
ZwCompressKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtConnectPort
    .globl ZwConnectPort
    .p2align 5
NtConnectPort:
ZwConnectPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtContinueEx
    .globl ZwContinueEx
    .p2align 5
NtContinueEx:
ZwContinueEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtConvertBetweenAuxiliaryCounterAndPerformanceCounter
    .globl ZwConvertBetweenAuxiliaryCounterAndPerformanceCounter
    .p2align 5
NtConvertBetweenAuxiliaryCounterAndPerformanceCounter:
ZwConvertBetweenAuxiliaryCounterAndPerformanceCounter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCopyFileChunk
    .globl ZwCopyFileChunk
    .p2align 5
NtCopyFileChunk:
ZwCopyFileChunk:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateCpuPartition
    .globl ZwCreateCpuPartition
    .p2align 5
NtCreateCpuPartition:
ZwCreateCpuPartition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateCrossVmEvent
    .globl ZwCreateCrossVmEvent
    .p2align 5
NtCreateCrossVmEvent:
ZwCreateCrossVmEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xA9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateCrossVmMutant
    .globl ZwCreateCrossVmMutant
    .p2align 5
NtCreateCrossVmMutant:
ZwCreateCrossVmMutant:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xAA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateDebugObject
    .globl ZwCreateDebugObject
    .p2align 5
NtCreateDebugObject:
ZwCreateDebugObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xAB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateDirectoryObject
    .globl ZwCreateDirectoryObject
    .p2align 5
NtCreateDirectoryObject:
ZwCreateDirectoryObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xAC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateDirectoryObjectEx
    .globl ZwCreateDirectoryObjectEx
    .p2align 5
NtCreateDirectoryObjectEx:
ZwCreateDirectoryObjectEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xAD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateEnclave
    .globl ZwCreateEnclave
    .p2align 5
NtCreateEnclave:
ZwCreateEnclave:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xAE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateEnlistment
    .globl ZwCreateEnlistment
    .p2align 5
NtCreateEnlistment:
ZwCreateEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xAF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateEventPair
    .globl ZwCreateEventPair
    .p2align 5
NtCreateEventPair:
ZwCreateEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateIRTimer
    .globl ZwCreateIRTimer
    .p2align 5
NtCreateIRTimer:
ZwCreateIRTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateIoCompletion
    .globl ZwCreateIoCompletion
    .p2align 5
NtCreateIoCompletion:
ZwCreateIoCompletion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateIoRing
    .globl ZwCreateIoRing
    .p2align 5
NtCreateIoRing:
ZwCreateIoRing:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateJobObject
    .globl ZwCreateJobObject
    .p2align 5
NtCreateJobObject:
ZwCreateJobObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateJobSet
    .globl ZwCreateJobSet
    .p2align 5
NtCreateJobSet:
ZwCreateJobSet:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateKeyTransacted
    .globl ZwCreateKeyTransacted
    .p2align 5
NtCreateKeyTransacted:
ZwCreateKeyTransacted:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateKeyedEvent
    .globl ZwCreateKeyedEvent
    .p2align 5
NtCreateKeyedEvent:
ZwCreateKeyedEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateLowBoxToken
    .globl ZwCreateLowBoxToken
    .p2align 5
NtCreateLowBoxToken:
ZwCreateLowBoxToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateMailslotFile
    .globl ZwCreateMailslotFile
    .p2align 5
NtCreateMailslotFile:
ZwCreateMailslotFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xB9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateMutant
    .globl ZwCreateMutant
    .p2align 5
NtCreateMutant:
ZwCreateMutant:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xBA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateNamedPipeFile
    .globl ZwCreateNamedPipeFile
    .p2align 5
NtCreateNamedPipeFile:
ZwCreateNamedPipeFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xBB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreatePagingFile
    .globl ZwCreatePagingFile
    .p2align 5
NtCreatePagingFile:
ZwCreatePagingFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xBC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreatePartition
    .globl ZwCreatePartition
    .p2align 5
NtCreatePartition:
ZwCreatePartition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xBD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreatePort
    .globl ZwCreatePort
    .p2align 5
NtCreatePort:
ZwCreatePort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xBE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreatePrivateNamespace
    .globl ZwCreatePrivateNamespace
    .p2align 5
NtCreatePrivateNamespace:
ZwCreatePrivateNamespace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xBF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateProcess
    .globl ZwCreateProcess
    .p2align 5
NtCreateProcess:
ZwCreateProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateProcessStateChange
    .globl ZwCreateProcessStateChange
    .p2align 5
NtCreateProcessStateChange:
ZwCreateProcessStateChange:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateProfile
    .globl ZwCreateProfile
    .p2align 5
NtCreateProfile:
ZwCreateProfile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateProfileEx
    .globl ZwCreateProfileEx
    .p2align 5
NtCreateProfileEx:
ZwCreateProfileEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateRegistryTransaction
    .globl ZwCreateRegistryTransaction
    .p2align 5
NtCreateRegistryTransaction:
ZwCreateRegistryTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateResourceManager
    .globl ZwCreateResourceManager
    .p2align 5
NtCreateResourceManager:
ZwCreateResourceManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateSectionEx
    .globl ZwCreateSectionEx
    .p2align 5
NtCreateSectionEx:
ZwCreateSectionEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateSemaphore
    .globl ZwCreateSemaphore
    .p2align 5
NtCreateSemaphore:
ZwCreateSemaphore:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateSymbolicLinkObject
    .globl ZwCreateSymbolicLinkObject
    .p2align 5
NtCreateSymbolicLinkObject:
ZwCreateSymbolicLinkObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateThreadEx
    .globl ZwCreateThreadEx
    .p2align 5
NtCreateThreadEx:
ZwCreateThreadEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xC9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateThreadStateChange
    .globl ZwCreateThreadStateChange
    .p2align 5
NtCreateThreadStateChange:
ZwCreateThreadStateChange:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xCA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateTimer
    .globl ZwCreateTimer
    .p2align 5
NtCreateTimer:
ZwCreateTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xCB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateTimer2
    .globl ZwCreateTimer2
    .p2align 5
NtCreateTimer2:
ZwCreateTimer2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xCC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateToken
    .globl ZwCreateToken
    .p2align 5
NtCreateToken:
ZwCreateToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xCD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateTokenEx
    .globl ZwCreateTokenEx
    .p2align 5
NtCreateTokenEx:
ZwCreateTokenEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xCE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateTransaction
    .globl ZwCreateTransaction
    .p2align 5
NtCreateTransaction:
ZwCreateTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xCF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateTransactionManager
    .globl ZwCreateTransactionManager
    .p2align 5
NtCreateTransactionManager:
ZwCreateTransactionManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateUserProcess
    .globl ZwCreateUserProcess
    .p2align 5
NtCreateUserProcess:
ZwCreateUserProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateWaitCompletionPacket
    .globl ZwCreateWaitCompletionPacket
    .p2align 5
NtCreateWaitCompletionPacket:
ZwCreateWaitCompletionPacket:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateWaitablePort
    .globl ZwCreateWaitablePort
    .p2align 5
NtCreateWaitablePort:
ZwCreateWaitablePort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateWnfStateName
    .globl ZwCreateWnfStateName
    .p2align 5
NtCreateWnfStateName:
ZwCreateWnfStateName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateWorkerFactory
    .globl ZwCreateWorkerFactory
    .p2align 5
NtCreateWorkerFactory:
ZwCreateWorkerFactory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDebugActiveProcess
    .globl ZwDebugActiveProcess
    .p2align 5
NtDebugActiveProcess:
ZwDebugActiveProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDebugContinue
    .globl ZwDebugContinue
    .p2align 5
NtDebugContinue:
ZwDebugContinue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteAtom
    .globl ZwDeleteAtom
    .p2align 5
NtDeleteAtom:
ZwDeleteAtom:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteBootEntry
    .globl ZwDeleteBootEntry
    .p2align 5
NtDeleteBootEntry:
ZwDeleteBootEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xD9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteDriverEntry
    .globl ZwDeleteDriverEntry
    .p2align 5
NtDeleteDriverEntry:
ZwDeleteDriverEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xDA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteFile
    .globl ZwDeleteFile
    .p2align 5
NtDeleteFile:
ZwDeleteFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xDB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteKey
    .globl ZwDeleteKey
    .p2align 5
NtDeleteKey:
ZwDeleteKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xDC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteObjectAuditAlarm
    .globl ZwDeleteObjectAuditAlarm
    .p2align 5
NtDeleteObjectAuditAlarm:
ZwDeleteObjectAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xDD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeletePrivateNamespace
    .globl ZwDeletePrivateNamespace
    .p2align 5
NtDeletePrivateNamespace:
ZwDeletePrivateNamespace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xDE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteValueKey
    .globl ZwDeleteValueKey
    .p2align 5
NtDeleteValueKey:
ZwDeleteValueKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xDF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteWnfStateData
    .globl ZwDeleteWnfStateData
    .p2align 5
NtDeleteWnfStateData:
ZwDeleteWnfStateData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDeleteWnfStateName
    .globl ZwDeleteWnfStateName
    .p2align 5
NtDeleteWnfStateName:
ZwDeleteWnfStateName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDirectGraphicsCall
    .globl ZwDirectGraphicsCall
    .p2align 5
NtDirectGraphicsCall:
ZwDirectGraphicsCall:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDisableLastKnownGood
    .globl ZwDisableLastKnownGood
    .p2align 5
NtDisableLastKnownGood:
ZwDisableLastKnownGood:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDisplayString
    .globl ZwDisplayString
    .p2align 5
NtDisplayString:
ZwDisplayString:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDrawText
    .globl ZwDrawText
    .p2align 5
NtDrawText:
ZwDrawText:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnableLastKnownGood
    .globl ZwEnableLastKnownGood
    .p2align 5
NtEnableLastKnownGood:
ZwEnableLastKnownGood:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnumerateBootEntries
    .globl ZwEnumerateBootEntries
    .p2align 5
NtEnumerateBootEntries:
ZwEnumerateBootEntries:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnumerateDriverEntries
    .globl ZwEnumerateDriverEntries
    .p2align 5
NtEnumerateDriverEntries:
ZwEnumerateDriverEntries:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnumerateSystemEnvironmentValuesEx
    .globl ZwEnumerateSystemEnvironmentValuesEx
    .p2align 5
NtEnumerateSystemEnvironmentValuesEx:
ZwEnumerateSystemEnvironmentValuesEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xE9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnumerateTransactionObject
    .globl ZwEnumerateTransactionObject
    .p2align 5
NtEnumerateTransactionObject:
ZwEnumerateTransactionObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xEA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtExtendSection
    .globl ZwExtendSection
    .p2align 5
NtExtendSection:
ZwExtendSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xEB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFilterBootOption
    .globl ZwFilterBootOption
    .p2align 5
NtFilterBootOption:
ZwFilterBootOption:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xEC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFilterToken
    .globl ZwFilterToken
    .p2align 5
NtFilterToken:
ZwFilterToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xED, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFilterTokenEx
    .globl ZwFilterTokenEx
    .p2align 5
NtFilterTokenEx:
ZwFilterTokenEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xEE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushBuffersFileEx
    .globl ZwFlushBuffersFileEx
    .p2align 5
NtFlushBuffersFileEx:
ZwFlushBuffersFileEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xEF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushInstallUILanguage
    .globl ZwFlushInstallUILanguage
    .p2align 5
NtFlushInstallUILanguage:
ZwFlushInstallUILanguage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushInstructionCache
    .globl ZwFlushInstructionCache
    .p2align 5
NtFlushInstructionCache:
ZwFlushInstructionCache:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushKey
    .globl ZwFlushKey
    .p2align 5
NtFlushKey:
ZwFlushKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushProcessWriteBuffers
    .globl ZwFlushProcessWriteBuffers
    .p2align 5
NtFlushProcessWriteBuffers:
ZwFlushProcessWriteBuffers:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushVirtualMemory
    .globl ZwFlushVirtualMemory
    .p2align 5
NtFlushVirtualMemory:
ZwFlushVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlushWriteBuffer
    .globl ZwFlushWriteBuffer
    .p2align 5
NtFlushWriteBuffer:
ZwFlushWriteBuffer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFreeUserPhysicalPages
    .globl ZwFreeUserPhysicalPages
    .p2align 5
NtFreeUserPhysicalPages:
ZwFreeUserPhysicalPages:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFreezeRegistry
    .globl ZwFreezeRegistry
    .p2align 5
NtFreezeRegistry:
ZwFreezeRegistry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFreezeTransactions
    .globl ZwFreezeTransactions
    .p2align 5
NtFreezeTransactions:
ZwFreezeTransactions:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetCachedSigningLevel
    .globl ZwGetCachedSigningLevel
    .p2align 5
NtGetCachedSigningLevel:
ZwGetCachedSigningLevel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xF9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetCompleteWnfStateSubscription
    .globl ZwGetCompleteWnfStateSubscription
    .p2align 5
NtGetCompleteWnfStateSubscription:
ZwGetCompleteWnfStateSubscription:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xFA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetContextThread
    .globl ZwGetContextThread
    .p2align 5
NtGetContextThread:
ZwGetContextThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xFB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetCurrentProcessorNumber
    .globl ZwGetCurrentProcessorNumber
    .p2align 5
NtGetCurrentProcessorNumber:
ZwGetCurrentProcessorNumber:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xFC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetCurrentProcessorNumberEx
    .globl ZwGetCurrentProcessorNumberEx
    .p2align 5
NtGetCurrentProcessorNumberEx:
ZwGetCurrentProcessorNumberEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xFD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetDevicePowerState
    .globl ZwGetDevicePowerState
    .p2align 5
NtGetDevicePowerState:
ZwGetDevicePowerState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xFE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetMUIRegistryInfo
    .globl ZwGetMUIRegistryInfo
    .p2align 5
NtGetMUIRegistryInfo:
ZwGetMUIRegistryInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0xFF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetNextProcess
    .globl ZwGetNextProcess
    .p2align 5
NtGetNextProcess:
ZwGetNextProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x100, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetNextThread
    .globl ZwGetNextThread
    .p2align 5
NtGetNextThread:
ZwGetNextThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x101, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetNlsSectionPtr
    .globl ZwGetNlsSectionPtr
    .p2align 5
NtGetNlsSectionPtr:
ZwGetNlsSectionPtr:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x102, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetNotificationResourceManager
    .globl ZwGetNotificationResourceManager
    .p2align 5
NtGetNotificationResourceManager:
ZwGetNotificationResourceManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x103, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGetWriteWatch
    .globl ZwGetWriteWatch
    .p2align 5
NtGetWriteWatch:
ZwGetWriteWatch:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x104, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtImpersonateAnonymousToken
    .globl ZwImpersonateAnonymousToken
    .p2align 5
NtImpersonateAnonymousToken:
ZwImpersonateAnonymousToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x105, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtImpersonateThread
    .globl ZwImpersonateThread
    .p2align 5
NtImpersonateThread:
ZwImpersonateThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x106, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtInitializeEnclave
    .globl ZwInitializeEnclave
    .p2align 5
NtInitializeEnclave:
ZwInitializeEnclave:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x107, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtInitializeNlsFiles
    .globl ZwInitializeNlsFiles
    .p2align 5
NtInitializeNlsFiles:
ZwInitializeNlsFiles:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x108, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtInitializeRegistry
    .globl ZwInitializeRegistry
    .p2align 5
NtInitializeRegistry:
ZwInitializeRegistry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x109, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtInitiatePowerAction
    .globl ZwInitiatePowerAction
    .p2align 5
NtInitiatePowerAction:
ZwInitiatePowerAction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtIsSystemResumeAutomatic
    .globl ZwIsSystemResumeAutomatic
    .p2align 5
NtIsSystemResumeAutomatic:
ZwIsSystemResumeAutomatic:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtIsUILanguageComitted
    .globl ZwIsUILanguageComitted
    .p2align 5
NtIsUILanguageComitted:
ZwIsUILanguageComitted:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtListenPort
    .globl ZwListenPort
    .p2align 5
NtListenPort:
ZwListenPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLoadDriver
    .globl ZwLoadDriver
    .p2align 5
NtLoadDriver:
ZwLoadDriver:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLoadEnclaveData
    .globl ZwLoadEnclaveData
    .p2align 5
NtLoadEnclaveData:
ZwLoadEnclaveData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLoadKey
    .globl ZwLoadKey
    .p2align 5
NtLoadKey:
ZwLoadKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x110, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLoadKey2
    .globl ZwLoadKey2
    .p2align 5
NtLoadKey2:
ZwLoadKey2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x111, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLoadKey3
    .globl ZwLoadKey3
    .p2align 5
NtLoadKey3:
ZwLoadKey3:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x112, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLoadKeyEx
    .globl ZwLoadKeyEx
    .p2align 5
NtLoadKeyEx:
ZwLoadKeyEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x113, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLockFile
    .globl ZwLockFile
    .p2align 5
NtLockFile:
ZwLockFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x114, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLockProductActivationKeys
    .globl ZwLockProductActivationKeys
    .p2align 5
NtLockProductActivationKeys:
ZwLockProductActivationKeys:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x115, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLockRegistryKey
    .globl ZwLockRegistryKey
    .p2align 5
NtLockRegistryKey:
ZwLockRegistryKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x116, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtLockVirtualMemory
    .globl ZwLockVirtualMemory
    .p2align 5
NtLockVirtualMemory:
ZwLockVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x117, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMakePermanentObject
    .globl ZwMakePermanentObject
    .p2align 5
NtMakePermanentObject:
ZwMakePermanentObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x118, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMakeTemporaryObject
    .globl ZwMakeTemporaryObject
    .p2align 5
NtMakeTemporaryObject:
ZwMakeTemporaryObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x119, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtManageHotPatch
    .globl ZwManageHotPatch
    .p2align 5
NtManageHotPatch:
ZwManageHotPatch:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtManagePartition
    .globl ZwManagePartition
    .p2align 5
NtManagePartition:
ZwManagePartition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMapCMFModule
    .globl ZwMapCMFModule
    .p2align 5
NtMapCMFModule:
ZwMapCMFModule:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMapUserPhysicalPages
    .globl ZwMapUserPhysicalPages
    .p2align 5
NtMapUserPhysicalPages:
ZwMapUserPhysicalPages:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMapViewOfSectionEx
    .globl ZwMapViewOfSectionEx
    .p2align 5
NtMapViewOfSectionEx:
ZwMapViewOfSectionEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModifyBootEntry
    .globl ZwModifyBootEntry
    .p2align 5
NtModifyBootEntry:
ZwModifyBootEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModifyDriverEntry
    .globl ZwModifyDriverEntry
    .p2align 5
NtModifyDriverEntry:
ZwModifyDriverEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x120, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtNotifyChangeDirectoryFile
    .globl ZwNotifyChangeDirectoryFile
    .p2align 5
NtNotifyChangeDirectoryFile:
ZwNotifyChangeDirectoryFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x121, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtNotifyChangeDirectoryFileEx
    .globl ZwNotifyChangeDirectoryFileEx
    .p2align 5
NtNotifyChangeDirectoryFileEx:
ZwNotifyChangeDirectoryFileEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x122, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtNotifyChangeKey
    .globl ZwNotifyChangeKey
    .p2align 5
NtNotifyChangeKey:
ZwNotifyChangeKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x123, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtNotifyChangeMultipleKeys
    .globl ZwNotifyChangeMultipleKeys
    .p2align 5
NtNotifyChangeMultipleKeys:
ZwNotifyChangeMultipleKeys:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x124, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtNotifyChangeSession
    .globl ZwNotifyChangeSession
    .p2align 5
NtNotifyChangeSession:
ZwNotifyChangeSession:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x125, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenCpuPartition
    .globl ZwOpenCpuPartition
    .p2align 5
NtOpenCpuPartition:
ZwOpenCpuPartition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x126, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenEnlistment
    .globl ZwOpenEnlistment
    .p2align 5
NtOpenEnlistment:
ZwOpenEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x127, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenEventPair
    .globl ZwOpenEventPair
    .p2align 5
NtOpenEventPair:
ZwOpenEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x128, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenIoCompletion
    .globl ZwOpenIoCompletion
    .p2align 5
NtOpenIoCompletion:
ZwOpenIoCompletion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x129, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenJobObject
    .globl ZwOpenJobObject
    .p2align 5
NtOpenJobObject:
ZwOpenJobObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenKeyEx
    .globl ZwOpenKeyEx
    .p2align 5
NtOpenKeyEx:
ZwOpenKeyEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenKeyTransacted
    .globl ZwOpenKeyTransacted
    .p2align 5
NtOpenKeyTransacted:
ZwOpenKeyTransacted:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenKeyTransactedEx
    .globl ZwOpenKeyTransactedEx
    .p2align 5
NtOpenKeyTransactedEx:
ZwOpenKeyTransactedEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenKeyedEvent
    .globl ZwOpenKeyedEvent
    .p2align 5
NtOpenKeyedEvent:
ZwOpenKeyedEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenMutant
    .globl ZwOpenMutant
    .p2align 5
NtOpenMutant:
ZwOpenMutant:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenObjectAuditAlarm
    .globl ZwOpenObjectAuditAlarm
    .p2align 5
NtOpenObjectAuditAlarm:
ZwOpenObjectAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x130, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenPartition
    .globl ZwOpenPartition
    .p2align 5
NtOpenPartition:
ZwOpenPartition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x131, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenPrivateNamespace
    .globl ZwOpenPrivateNamespace
    .p2align 5
NtOpenPrivateNamespace:
ZwOpenPrivateNamespace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x132, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenProcessToken
    .globl ZwOpenProcessToken
    .p2align 5
NtOpenProcessToken:
ZwOpenProcessToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x133, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenRegistryTransaction
    .globl ZwOpenRegistryTransaction
    .p2align 5
NtOpenRegistryTransaction:
ZwOpenRegistryTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x134, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenResourceManager
    .globl ZwOpenResourceManager
    .p2align 5
NtOpenResourceManager:
ZwOpenResourceManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x135, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenSemaphore
    .globl ZwOpenSemaphore
    .p2align 5
NtOpenSemaphore:
ZwOpenSemaphore:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x136, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenSession
    .globl ZwOpenSession
    .p2align 5
NtOpenSession:
ZwOpenSession:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x137, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenSymbolicLinkObject
    .globl ZwOpenSymbolicLinkObject
    .p2align 5
NtOpenSymbolicLinkObject:
ZwOpenSymbolicLinkObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x138, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenThread
    .globl ZwOpenThread
    .p2align 5
NtOpenThread:
ZwOpenThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x139, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenTimer
    .globl ZwOpenTimer
    .p2align 5
NtOpenTimer:
ZwOpenTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenTransaction
    .globl ZwOpenTransaction
    .p2align 5
NtOpenTransaction:
ZwOpenTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenTransactionManager
    .globl ZwOpenTransactionManager
    .p2align 5
NtOpenTransactionManager:
ZwOpenTransactionManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPlugPlayControl
    .globl ZwPlugPlayControl
    .p2align 5
NtPlugPlayControl:
ZwPlugPlayControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPrePrepareComplete
    .globl ZwPrePrepareComplete
    .p2align 5
NtPrePrepareComplete:
ZwPrePrepareComplete:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPrePrepareEnlistment
    .globl ZwPrePrepareEnlistment
    .p2align 5
NtPrePrepareEnlistment:
ZwPrePrepareEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPrepareComplete
    .globl ZwPrepareComplete
    .p2align 5
NtPrepareComplete:
ZwPrepareComplete:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x140, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPrepareEnlistment
    .globl ZwPrepareEnlistment
    .p2align 5
NtPrepareEnlistment:
ZwPrepareEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x141, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPrivilegeCheck
    .globl ZwPrivilegeCheck
    .p2align 5
NtPrivilegeCheck:
ZwPrivilegeCheck:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x142, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPrivilegeObjectAuditAlarm
    .globl ZwPrivilegeObjectAuditAlarm
    .p2align 5
NtPrivilegeObjectAuditAlarm:
ZwPrivilegeObjectAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x143, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPrivilegedServiceAuditAlarm
    .globl ZwPrivilegedServiceAuditAlarm
    .p2align 5
NtPrivilegedServiceAuditAlarm:
ZwPrivilegedServiceAuditAlarm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x144, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPropagationComplete
    .globl ZwPropagationComplete
    .p2align 5
NtPropagationComplete:
ZwPropagationComplete:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x145, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPropagationFailed
    .globl ZwPropagationFailed
    .p2align 5
NtPropagationFailed:
ZwPropagationFailed:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x146, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPssCaptureVaSpaceBulk
    .globl ZwPssCaptureVaSpaceBulk
    .p2align 5
NtPssCaptureVaSpaceBulk:
ZwPssCaptureVaSpaceBulk:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x147, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtPulseEvent
    .globl ZwPulseEvent
    .p2align 5
NtPulseEvent:
ZwPulseEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x148, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryAuxiliaryCounterFrequency
    .globl ZwQueryAuxiliaryCounterFrequency
    .p2align 5
NtQueryAuxiliaryCounterFrequency:
ZwQueryAuxiliaryCounterFrequency:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x149, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryBootEntryOrder
    .globl ZwQueryBootEntryOrder
    .p2align 5
NtQueryBootEntryOrder:
ZwQueryBootEntryOrder:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryBootOptions
    .globl ZwQueryBootOptions
    .p2align 5
NtQueryBootOptions:
ZwQueryBootOptions:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryDebugFilterState
    .globl ZwQueryDebugFilterState
    .p2align 5
NtQueryDebugFilterState:
ZwQueryDebugFilterState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryDirectoryFileEx
    .globl ZwQueryDirectoryFileEx
    .p2align 5
NtQueryDirectoryFileEx:
ZwQueryDirectoryFileEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryDirectoryObject
    .globl ZwQueryDirectoryObject
    .p2align 5
NtQueryDirectoryObject:
ZwQueryDirectoryObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryDriverEntryOrder
    .globl ZwQueryDriverEntryOrder
    .p2align 5
NtQueryDriverEntryOrder:
ZwQueryDriverEntryOrder:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryEaFile
    .globl ZwQueryEaFile
    .p2align 5
NtQueryEaFile:
ZwQueryEaFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x150, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryFullAttributesFile
    .globl ZwQueryFullAttributesFile
    .p2align 5
NtQueryFullAttributesFile:
ZwQueryFullAttributesFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x151, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationAtom
    .globl ZwQueryInformationAtom
    .p2align 5
NtQueryInformationAtom:
ZwQueryInformationAtom:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x152, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationByName
    .globl ZwQueryInformationByName
    .p2align 5
NtQueryInformationByName:
ZwQueryInformationByName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x153, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationCpuPartition
    .globl ZwQueryInformationCpuPartition
    .p2align 5
NtQueryInformationCpuPartition:
ZwQueryInformationCpuPartition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x154, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationEnlistment
    .globl ZwQueryInformationEnlistment
    .p2align 5
NtQueryInformationEnlistment:
ZwQueryInformationEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x155, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationJobObject
    .globl ZwQueryInformationJobObject
    .p2align 5
NtQueryInformationJobObject:
ZwQueryInformationJobObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x156, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationPort
    .globl ZwQueryInformationPort
    .p2align 5
NtQueryInformationPort:
ZwQueryInformationPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x157, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationResourceManager
    .globl ZwQueryInformationResourceManager
    .p2align 5
NtQueryInformationResourceManager:
ZwQueryInformationResourceManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x158, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationTransaction
    .globl ZwQueryInformationTransaction
    .p2align 5
NtQueryInformationTransaction:
ZwQueryInformationTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x159, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationTransactionManager
    .globl ZwQueryInformationTransactionManager
    .p2align 5
NtQueryInformationTransactionManager:
ZwQueryInformationTransactionManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInformationWorkerFactory
    .globl ZwQueryInformationWorkerFactory
    .p2align 5
NtQueryInformationWorkerFactory:
ZwQueryInformationWorkerFactory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryInstallUILanguage
    .globl ZwQueryInstallUILanguage
    .p2align 5
NtQueryInstallUILanguage:
ZwQueryInstallUILanguage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryIntervalProfile
    .globl ZwQueryIntervalProfile
    .p2align 5
NtQueryIntervalProfile:
ZwQueryIntervalProfile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryIoCompletion
    .globl ZwQueryIoCompletion
    .p2align 5
NtQueryIoCompletion:
ZwQueryIoCompletion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryIoRingCapabilities
    .globl ZwQueryIoRingCapabilities
    .p2align 5
NtQueryIoRingCapabilities:
ZwQueryIoRingCapabilities:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryLicenseValue
    .globl ZwQueryLicenseValue
    .p2align 5
NtQueryLicenseValue:
ZwQueryLicenseValue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x160, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryMultipleValueKey
    .globl ZwQueryMultipleValueKey
    .p2align 5
NtQueryMultipleValueKey:
ZwQueryMultipleValueKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x161, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryMutant
    .globl ZwQueryMutant
    .p2align 5
NtQueryMutant:
ZwQueryMutant:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x162, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryOpenSubKeys
    .globl ZwQueryOpenSubKeys
    .p2align 5
NtQueryOpenSubKeys:
ZwQueryOpenSubKeys:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x163, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryOpenSubKeysEx
    .globl ZwQueryOpenSubKeysEx
    .p2align 5
NtQueryOpenSubKeysEx:
ZwQueryOpenSubKeysEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x164, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryPortInformationProcess
    .globl ZwQueryPortInformationProcess
    .p2align 5
NtQueryPortInformationProcess:
ZwQueryPortInformationProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x165, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryQuotaInformationFile
    .globl ZwQueryQuotaInformationFile
    .p2align 5
NtQueryQuotaInformationFile:
ZwQueryQuotaInformationFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x166, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySecurityAttributesToken
    .globl ZwQuerySecurityAttributesToken
    .p2align 5
NtQuerySecurityAttributesToken:
ZwQuerySecurityAttributesToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x167, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySecurityObject
    .globl ZwQuerySecurityObject
    .p2align 5
NtQuerySecurityObject:
ZwQuerySecurityObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x168, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySecurityPolicy
    .globl ZwQuerySecurityPolicy
    .p2align 5
NtQuerySecurityPolicy:
ZwQuerySecurityPolicy:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x169, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySemaphore
    .globl ZwQuerySemaphore
    .p2align 5
NtQuerySemaphore:
ZwQuerySemaphore:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x16A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySymbolicLinkObject
    .globl ZwQuerySymbolicLinkObject
    .p2align 5
NtQuerySymbolicLinkObject:
ZwQuerySymbolicLinkObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x16B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySystemEnvironmentValue
    .globl ZwQuerySystemEnvironmentValue
    .p2align 5
NtQuerySystemEnvironmentValue:
ZwQuerySystemEnvironmentValue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x16C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySystemEnvironmentValueEx
    .globl ZwQuerySystemEnvironmentValueEx
    .p2align 5
NtQuerySystemEnvironmentValueEx:
ZwQuerySystemEnvironmentValueEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x16D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQuerySystemInformationEx
    .globl ZwQuerySystemInformationEx
    .p2align 5
NtQuerySystemInformationEx:
ZwQuerySystemInformationEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x16E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryTimerResolution
    .globl ZwQueryTimerResolution
    .p2align 5
NtQueryTimerResolution:
ZwQueryTimerResolution:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x16F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryWnfStateData
    .globl ZwQueryWnfStateData
    .p2align 5
NtQueryWnfStateData:
ZwQueryWnfStateData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x170, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryWnfStateNameInformation
    .globl ZwQueryWnfStateNameInformation
    .p2align 5
NtQueryWnfStateNameInformation:
ZwQueryWnfStateNameInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x171, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueueApcThreadEx
    .globl ZwQueueApcThreadEx
    .p2align 5
NtQueueApcThreadEx:
ZwQueueApcThreadEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x172, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueueApcThreadEx2
    .globl ZwQueueApcThreadEx2
    .p2align 5
NtQueueApcThreadEx2:
ZwQueueApcThreadEx2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x173, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRaiseException
    .globl ZwRaiseException
    .p2align 5
NtRaiseException:
ZwRaiseException:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x174, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRaiseHardError
    .globl ZwRaiseHardError
    .p2align 5
NtRaiseHardError:
ZwRaiseHardError:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x175, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReadOnlyEnlistment
    .globl ZwReadOnlyEnlistment
    .p2align 5
NtReadOnlyEnlistment:
ZwReadOnlyEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x176, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReadVirtualMemoryEx
    .globl ZwReadVirtualMemoryEx
    .p2align 5
NtReadVirtualMemoryEx:
ZwReadVirtualMemoryEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x177, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRecoverEnlistment
    .globl ZwRecoverEnlistment
    .p2align 5
NtRecoverEnlistment:
ZwRecoverEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x178, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRecoverResourceManager
    .globl ZwRecoverResourceManager
    .p2align 5
NtRecoverResourceManager:
ZwRecoverResourceManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x179, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRecoverTransactionManager
    .globl ZwRecoverTransactionManager
    .p2align 5
NtRecoverTransactionManager:
ZwRecoverTransactionManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x17A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRegisterProtocolAddressInformation
    .globl ZwRegisterProtocolAddressInformation
    .p2align 5
NtRegisterProtocolAddressInformation:
ZwRegisterProtocolAddressInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x17B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRegisterThreadTerminatePort
    .globl ZwRegisterThreadTerminatePort
    .p2align 5
NtRegisterThreadTerminatePort:
ZwRegisterThreadTerminatePort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x17C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReleaseKeyedEvent
    .globl ZwReleaseKeyedEvent
    .p2align 5
NtReleaseKeyedEvent:
ZwReleaseKeyedEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x17D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReleaseWorkerFactoryWorker
    .globl ZwReleaseWorkerFactoryWorker
    .p2align 5
NtReleaseWorkerFactoryWorker:
ZwReleaseWorkerFactoryWorker:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x17E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRemoveIoCompletionEx
    .globl ZwRemoveIoCompletionEx
    .p2align 5
NtRemoveIoCompletionEx:
ZwRemoveIoCompletionEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x17F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRemoveProcessDebug
    .globl ZwRemoveProcessDebug
    .p2align 5
NtRemoveProcessDebug:
ZwRemoveProcessDebug:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x180, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRenameKey
    .globl ZwRenameKey
    .p2align 5
NtRenameKey:
ZwRenameKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x181, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRenameTransactionManager
    .globl ZwRenameTransactionManager
    .p2align 5
NtRenameTransactionManager:
ZwRenameTransactionManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x182, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReplaceKey
    .globl ZwReplaceKey
    .p2align 5
NtReplaceKey:
ZwReplaceKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x183, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReplacePartitionUnit
    .globl ZwReplacePartitionUnit
    .p2align 5
NtReplacePartitionUnit:
ZwReplacePartitionUnit:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x184, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtReplyWaitReplyPort
    .globl ZwReplyWaitReplyPort
    .p2align 5
NtReplyWaitReplyPort:
ZwReplyWaitReplyPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x185, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRequestPort
    .globl ZwRequestPort
    .p2align 5
NtRequestPort:
ZwRequestPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x186, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtResetEvent
    .globl ZwResetEvent
    .p2align 5
NtResetEvent:
ZwResetEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x187, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtResetWriteWatch
    .globl ZwResetWriteWatch
    .p2align 5
NtResetWriteWatch:
ZwResetWriteWatch:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x188, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRestoreKey
    .globl ZwRestoreKey
    .p2align 5
NtRestoreKey:
ZwRestoreKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x189, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtResumeProcess
    .globl ZwResumeProcess
    .p2align 5
NtResumeProcess:
ZwResumeProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x18A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRevertContainerImpersonation
    .globl ZwRevertContainerImpersonation
    .p2align 5
NtRevertContainerImpersonation:
ZwRevertContainerImpersonation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x18B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRollbackComplete
    .globl ZwRollbackComplete
    .p2align 5
NtRollbackComplete:
ZwRollbackComplete:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x18C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRollbackEnlistment
    .globl ZwRollbackEnlistment
    .p2align 5
NtRollbackEnlistment:
ZwRollbackEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x18D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRollbackRegistryTransaction
    .globl ZwRollbackRegistryTransaction
    .p2align 5
NtRollbackRegistryTransaction:
ZwRollbackRegistryTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x18E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRollbackTransaction
    .globl ZwRollbackTransaction
    .p2align 5
NtRollbackTransaction:
ZwRollbackTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x18F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRollforwardTransactionManager
    .globl ZwRollforwardTransactionManager
    .p2align 5
NtRollforwardTransactionManager:
ZwRollforwardTransactionManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x190, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSaveKey
    .globl ZwSaveKey
    .p2align 5
NtSaveKey:
ZwSaveKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x191, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSaveKeyEx
    .globl ZwSaveKeyEx
    .p2align 5
NtSaveKeyEx:
ZwSaveKeyEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x192, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSaveMergedKeys
    .globl ZwSaveMergedKeys
    .p2align 5
NtSaveMergedKeys:
ZwSaveMergedKeys:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x193, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSecureConnectPort
    .globl ZwSecureConnectPort
    .p2align 5
NtSecureConnectPort:
ZwSecureConnectPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x194, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSerializeBoot
    .globl ZwSerializeBoot
    .p2align 5
NtSerializeBoot:
ZwSerializeBoot:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x195, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetBootEntryOrder
    .globl ZwSetBootEntryOrder
    .p2align 5
NtSetBootEntryOrder:
ZwSetBootEntryOrder:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x196, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetBootOptions
    .globl ZwSetBootOptions
    .p2align 5
NtSetBootOptions:
ZwSetBootOptions:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x197, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCachedSigningLevel
    .globl ZwSetCachedSigningLevel
    .p2align 5
NtSetCachedSigningLevel:
ZwSetCachedSigningLevel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x198, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCachedSigningLevel2
    .globl ZwSetCachedSigningLevel2
    .p2align 5
NtSetCachedSigningLevel2:
ZwSetCachedSigningLevel2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x199, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetContextThread
    .globl ZwSetContextThread
    .p2align 5
NtSetContextThread:
ZwSetContextThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x19A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetDebugFilterState
    .globl ZwSetDebugFilterState
    .p2align 5
NtSetDebugFilterState:
ZwSetDebugFilterState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x19B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetDefaultHardErrorPort
    .globl ZwSetDefaultHardErrorPort
    .p2align 5
NtSetDefaultHardErrorPort:
ZwSetDefaultHardErrorPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x19C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetDefaultLocale
    .globl ZwSetDefaultLocale
    .p2align 5
NtSetDefaultLocale:
ZwSetDefaultLocale:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x19D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetDefaultUILanguage
    .globl ZwSetDefaultUILanguage
    .p2align 5
NtSetDefaultUILanguage:
ZwSetDefaultUILanguage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x19E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetDriverEntryOrder
    .globl ZwSetDriverEntryOrder
    .p2align 5
NtSetDriverEntryOrder:
ZwSetDriverEntryOrder:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x19F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetEaFile
    .globl ZwSetEaFile
    .p2align 5
NtSetEaFile:
ZwSetEaFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetEventEx
    .globl ZwSetEventEx
    .p2align 5
NtSetEventEx:
ZwSetEventEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetHighEventPair
    .globl ZwSetHighEventPair
    .p2align 5
NtSetHighEventPair:
ZwSetHighEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetHighWaitLowEventPair
    .globl ZwSetHighWaitLowEventPair
    .p2align 5
NtSetHighWaitLowEventPair:
ZwSetHighWaitLowEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetIRTimer
    .globl ZwSetIRTimer
    .p2align 5
NtSetIRTimer:
ZwSetIRTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationCpuPartition
    .globl ZwSetInformationCpuPartition
    .p2align 5
NtSetInformationCpuPartition:
ZwSetInformationCpuPartition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationDebugObject
    .globl ZwSetInformationDebugObject
    .p2align 5
NtSetInformationDebugObject:
ZwSetInformationDebugObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationEnlistment
    .globl ZwSetInformationEnlistment
    .p2align 5
NtSetInformationEnlistment:
ZwSetInformationEnlistment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationIoRing
    .globl ZwSetInformationIoRing
    .p2align 5
NtSetInformationIoRing:
ZwSetInformationIoRing:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationJobObject
    .globl ZwSetInformationJobObject
    .p2align 5
NtSetInformationJobObject:
ZwSetInformationJobObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1A9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationKey
    .globl ZwSetInformationKey
    .p2align 5
NtSetInformationKey:
ZwSetInformationKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1AA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationResourceManager
    .globl ZwSetInformationResourceManager
    .p2align 5
NtSetInformationResourceManager:
ZwSetInformationResourceManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1AB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationSymbolicLink
    .globl ZwSetInformationSymbolicLink
    .p2align 5
NtSetInformationSymbolicLink:
ZwSetInformationSymbolicLink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1AC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationToken
    .globl ZwSetInformationToken
    .p2align 5
NtSetInformationToken:
ZwSetInformationToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1AD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationTransaction
    .globl ZwSetInformationTransaction
    .p2align 5
NtSetInformationTransaction:
ZwSetInformationTransaction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1AE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationTransactionManager
    .globl ZwSetInformationTransactionManager
    .p2align 5
NtSetInformationTransactionManager:
ZwSetInformationTransactionManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1AF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationVirtualMemory
    .globl ZwSetInformationVirtualMemory
    .p2align 5
NtSetInformationVirtualMemory:
ZwSetInformationVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetInformationWorkerFactory
    .globl ZwSetInformationWorkerFactory
    .p2align 5
NtSetInformationWorkerFactory:
ZwSetInformationWorkerFactory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetIntervalProfile
    .globl ZwSetIntervalProfile
    .p2align 5
NtSetIntervalProfile:
ZwSetIntervalProfile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetIoCompletion
    .globl ZwSetIoCompletion
    .p2align 5
NtSetIoCompletion:
ZwSetIoCompletion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetIoCompletionEx
    .globl ZwSetIoCompletionEx
    .p2align 5
NtSetIoCompletionEx:
ZwSetIoCompletionEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetLdtEntries
    .globl ZwSetLdtEntries
    .p2align 5
NtSetLdtEntries:
ZwSetLdtEntries:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetLowEventPair
    .globl ZwSetLowEventPair
    .p2align 5
NtSetLowEventPair:
ZwSetLowEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetLowWaitHighEventPair
    .globl ZwSetLowWaitHighEventPair
    .p2align 5
NtSetLowWaitHighEventPair:
ZwSetLowWaitHighEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetQuotaInformationFile
    .globl ZwSetQuotaInformationFile
    .p2align 5
NtSetQuotaInformationFile:
ZwSetQuotaInformationFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetSecurityObject
    .globl ZwSetSecurityObject
    .p2align 5
NtSetSecurityObject:
ZwSetSecurityObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1B9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetSystemEnvironmentValue
    .globl ZwSetSystemEnvironmentValue
    .p2align 5
NtSetSystemEnvironmentValue:
ZwSetSystemEnvironmentValue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1BA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetSystemEnvironmentValueEx
    .globl ZwSetSystemEnvironmentValueEx
    .p2align 5
NtSetSystemEnvironmentValueEx:
ZwSetSystemEnvironmentValueEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1BB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetSystemInformation
    .globl ZwSetSystemInformation
    .p2align 5
NtSetSystemInformation:
ZwSetSystemInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1BC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetSystemPowerState
    .globl ZwSetSystemPowerState
    .p2align 5
NtSetSystemPowerState:
ZwSetSystemPowerState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1BD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetSystemTime
    .globl ZwSetSystemTime
    .p2align 5
NtSetSystemTime:
ZwSetSystemTime:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1BE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetThreadExecutionState
    .globl ZwSetThreadExecutionState
    .p2align 5
NtSetThreadExecutionState:
ZwSetThreadExecutionState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1BF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetTimer2
    .globl ZwSetTimer2
    .p2align 5
NtSetTimer2:
ZwSetTimer2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetTimerEx
    .globl ZwSetTimerEx
    .p2align 5
NtSetTimerEx:
ZwSetTimerEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetTimerResolution
    .globl ZwSetTimerResolution
    .p2align 5
NtSetTimerResolution:
ZwSetTimerResolution:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetUuidSeed
    .globl ZwSetUuidSeed
    .p2align 5
NtSetUuidSeed:
ZwSetUuidSeed:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetVolumeInformationFile
    .globl ZwSetVolumeInformationFile
    .p2align 5
NtSetVolumeInformationFile:
ZwSetVolumeInformationFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetWnfProcessNotificationEvent
    .globl ZwSetWnfProcessNotificationEvent
    .p2align 5
NtSetWnfProcessNotificationEvent:
ZwSetWnfProcessNotificationEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtShutdownSystem
    .globl ZwShutdownSystem
    .p2align 5
NtShutdownSystem:
ZwShutdownSystem:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtShutdownWorkerFactory
    .globl ZwShutdownWorkerFactory
    .p2align 5
NtShutdownWorkerFactory:
ZwShutdownWorkerFactory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSignalAndWaitForSingleObject
    .globl ZwSignalAndWaitForSingleObject
    .p2align 5
NtSignalAndWaitForSingleObject:
ZwSignalAndWaitForSingleObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSinglePhaseReject
    .globl ZwSinglePhaseReject
    .p2align 5
NtSinglePhaseReject:
ZwSinglePhaseReject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1C9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtStartProfile
    .globl ZwStartProfile
    .p2align 5
NtStartProfile:
ZwStartProfile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1CA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtStopProfile
    .globl ZwStopProfile
    .p2align 5
NtStopProfile:
ZwStopProfile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1CB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSubmitIoRing
    .globl ZwSubmitIoRing
    .p2align 5
NtSubmitIoRing:
ZwSubmitIoRing:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1CC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSubscribeWnfStateChange
    .globl ZwSubscribeWnfStateChange
    .p2align 5
NtSubscribeWnfStateChange:
ZwSubscribeWnfStateChange:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1CD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSuspendProcess
    .globl ZwSuspendProcess
    .p2align 5
NtSuspendProcess:
ZwSuspendProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1CE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSuspendThread
    .globl ZwSuspendThread
    .p2align 5
NtSuspendThread:
ZwSuspendThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1CF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSystemDebugControl
    .globl ZwSystemDebugControl
    .p2align 5
NtSystemDebugControl:
ZwSystemDebugControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTerminateEnclave
    .globl ZwTerminateEnclave
    .p2align 5
NtTerminateEnclave:
ZwTerminateEnclave:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTerminateJobObject
    .globl ZwTerminateJobObject
    .p2align 5
NtTerminateJobObject:
ZwTerminateJobObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTestAlert
    .globl ZwTestAlert
    .p2align 5
NtTestAlert:
ZwTestAlert:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtThawRegistry
    .globl ZwThawRegistry
    .p2align 5
NtThawRegistry:
ZwThawRegistry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtThawTransactions
    .globl ZwThawTransactions
    .p2align 5
NtThawTransactions:
ZwThawTransactions:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTraceControl
    .globl ZwTraceControl
    .p2align 5
NtTraceControl:
ZwTraceControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTranslateFilePath
    .globl ZwTranslateFilePath
    .p2align 5
NtTranslateFilePath:
ZwTranslateFilePath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUmsThreadYield
    .globl ZwUmsThreadYield
    .p2align 5
NtUmsThreadYield:
ZwUmsThreadYield:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnloadDriver
    .globl ZwUnloadDriver
    .p2align 5
NtUnloadDriver:
ZwUnloadDriver:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1D9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnloadKey
    .globl ZwUnloadKey
    .p2align 5
NtUnloadKey:
ZwUnloadKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1DA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnloadKey2
    .globl ZwUnloadKey2
    .p2align 5
NtUnloadKey2:
ZwUnloadKey2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1DB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnloadKeyEx
    .globl ZwUnloadKeyEx
    .p2align 5
NtUnloadKeyEx:
ZwUnloadKeyEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1DC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnlockFile
    .globl ZwUnlockFile
    .p2align 5
NtUnlockFile:
ZwUnlockFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1DD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnlockVirtualMemory
    .globl ZwUnlockVirtualMemory
    .p2align 5
NtUnlockVirtualMemory:
ZwUnlockVirtualMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1DE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnmapViewOfSectionEx
    .globl ZwUnmapViewOfSectionEx
    .p2align 5
NtUnmapViewOfSectionEx:
ZwUnmapViewOfSectionEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1DF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnsubscribeWnfStateChange
    .globl ZwUnsubscribeWnfStateChange
    .p2align 5
NtUnsubscribeWnfStateChange:
ZwUnsubscribeWnfStateChange:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUpdateWnfStateData
    .globl ZwUpdateWnfStateData
    .p2align 5
NtUpdateWnfStateData:
ZwUpdateWnfStateData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtVdmControl
    .globl ZwVdmControl
    .p2align 5
NtVdmControl:
ZwVdmControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitForAlertByThreadId
    .globl ZwWaitForAlertByThreadId
    .p2align 5
NtWaitForAlertByThreadId:
ZwWaitForAlertByThreadId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitForDebugEvent
    .globl ZwWaitForDebugEvent
    .p2align 5
NtWaitForDebugEvent:
ZwWaitForDebugEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitForKeyedEvent
    .globl ZwWaitForKeyedEvent
    .p2align 5
NtWaitForKeyedEvent:
ZwWaitForKeyedEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitForWorkViaWorkerFactory
    .globl ZwWaitForWorkViaWorkerFactory
    .p2align 5
NtWaitForWorkViaWorkerFactory:
ZwWaitForWorkViaWorkerFactory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitHighEventPair
    .globl ZwWaitHighEventPair
    .p2align 5
NtWaitHighEventPair:
ZwWaitHighEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtWaitLowEventPair
    .globl ZwWaitLowEventPair
    .p2align 5
NtWaitLowEventPair:
ZwWaitLowEventPair:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1E8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
