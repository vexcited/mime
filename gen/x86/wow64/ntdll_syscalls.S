    .text
    .p2align 4, 0x90
    .globl _NtAlertThread@4
_NtAlertThread@4:
    movl $0x30070, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGetWriteWatch@28
_NtGetWriteWatch@28:
    movl $0x104, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtResetWriteWatch@12
_NtResetWriteWatch@12:
    movl $0xc0188, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtAllocateVirtualMemoryEx@28
_NtAllocateVirtualMemoryEx@28:
    movl $0x78, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtMapViewOfSectionEx@36
_NtMapViewOfSectionEx@36:
    movl $0x11e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtAllocateVirtualMemory@24
_NtAllocateVirtualMemory@24:
    movl $0x18, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtApphelpCacheControl@8
_NtApphelpCacheControl@8:
    movl $0x4c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtCallbackReturn@12
_NtCallbackReturn@12:
    movl $0x5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtCancelIoFile@8
_NtCancelIoFile@8:
    movl $0x5d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtCancelTimer@8
_NtCancelTimer@8:
    movl $0x70061, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtClearEvent@4
_NtClearEvent@4:
    movl $0x3003e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtClose@4
_NtClose@4:
    movl $0x3000f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtContinue@8
_NtContinue@8:
    movl $0x43, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGetContextThread@8
_NtGetContextThread@8:
    movl $0xfb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtCancelIoFileEx@12
_NtCancelIoFileEx@12:
    movl $0x94, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtCreateNamedPipeFile@56
_NtCreateNamedPipeFile@56:
    movl $0xbb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $56
    .p2align 4, 0x90
    .globl _NtCreateIoCompletion@16
_NtCreateIoCompletion@16:
    movl $0xb2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtSetIoCompletion@20
_NtSetIoCompletion@20:
    movl $0x1b3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtRemoveIoCompletion@20
_NtRemoveIoCompletion@20:
    movl $0x1c0009, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtRemoveIoCompletionEx@24
_NtRemoveIoCompletionEx@24:
    movl $0x17f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtOpenIoCompletion@12
_NtOpenIoCompletion@12:
    movl $0x129, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtSetIoCompletionEx@24
_NtSetIoCompletionEx@24:
    movl $0x1b4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtQueryIoCompletion@20
_NtQueryIoCompletion@20:
    movl $0x15e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtSetContextThread@8
_NtSetContextThread@8:
    movl $0x19a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtCreateDirectoryObject@12
_NtCreateDirectoryObject@12:
    movl $0xac, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtCreateSymbolicLinkObject@16
_NtCreateSymbolicLinkObject@16:
    movl $0xc8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtCreateEvent@20
_NtCreateEvent@20:
    movl $0x48, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtCreateFile@44
_NtCreateFile@44:
    movl $0x55, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $44
    .p2align 4, 0x90
    .globl _NtCreateKey@28
_NtCreateKey@28:
    movl $0x1d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtCreateMutant@16
_NtCreateMutant@16:
    movl $0xba, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtCreateSection@28
_NtCreateSection@28:
    movl $0x4a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtCreateSemaphore@20
_NtCreateSemaphore@20:
    movl $0xc7, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtCreateThreadEx@44
_NtCreateThreadEx@44:
    movl $0xc9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $44
    .p2align 4, 0x90
    .globl _NtCreateTimer@16
_NtCreateTimer@16:
    movl $0xcb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtDelayExecution@8
_NtDelayExecution@8:
    movl $0x60034, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtDeviceIoControlFile@40
_NtDeviceIoControlFile@40:
    movl $0x1b0007, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtDisplayString@4
_NtDisplayString@4:
    movl $0xe4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtDuplicateObject@28
_NtDuplicateObject@28:
    movl $0x3c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtFlushBuffersFile@8
_NtFlushBuffersFile@8:
    movl $0x4b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtFlushInstructionCache@12
_NtFlushInstructionCache@12:
    movl $0xc00f1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtFlushProcessWriteBuffers@0
_NtFlushProcessWriteBuffers@0:
    movl $0xf3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtFreeVirtualMemory@16
_NtFreeVirtualMemory@16:
    movl $0x1e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtFsControlFile@40
_NtFsControlFile@40:
    movl $0x1b0039, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtGetCurrentProcessorNumber@0
_NtGetCurrentProcessorNumber@0:
    movl $0xfc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtIsProcessInJob@8
_NtIsProcessInJob@8:
    movl $0x8004f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtMapViewOfSection@40
_NtMapViewOfSection@40:
    movl $0x28, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtOpenDirectoryObject@12
_NtOpenDirectoryObject@12:
    movl $0x58, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtPlugPlayControl@12
_NtPlugPlayControl@12:
    movl $0x13d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtQueryDirectoryObject@28
_NtQueryDirectoryObject@28:
    movl $0x14e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtOpenEvent@12
_NtOpenEvent@12:
    movl $0x40, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenFile@24
_NtOpenFile@24:
    movl $0x33, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtOpenKey@12
_NtOpenKey@12:
    movl $0x12, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenKeyEx@16
_NtOpenKeyEx@16:
    movl $0x12b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtOpenMutant@12
_NtOpenMutant@12:
    movl $0x12f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenProcess@16
_NtOpenProcess@16:
    movl $0x26, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtSuspendProcess@4
_NtSuspendProcess@4:
    movl $0x301ce, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtResumeProcess@4
_NtResumeProcess@4:
    movl $0x3018a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtSetSecurityObject@12
_NtSetSecurityObject@12:
    movl $0x1b9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenThread@16
_NtOpenThread@16:
    movl $0x139, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtCreateLowBoxToken@36
_NtCreateLowBoxToken@36:
    movl $0xb8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtAdjustGroupsToken@24
_NtAdjustGroupsToken@24:
    movl $0x6c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtAllocateLocallyUniqueId@4
_NtAllocateLocallyUniqueId@4:
    movl $0x40073, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtCompareTokens@12
_NtCompareTokens@12:
    movl $0xa1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtCreateWaitCompletionPacket@12
_NtCreateWaitCompletionPacket@12:
    movl $0xd2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtAssociateWaitCompletionPacket@32
_NtAssociateWaitCompletionPacket@32:
    movl $0x92, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $32
    .p2align 4, 0x90
    .globl _NtCancelWaitCompletionPacket@8
_NtCancelWaitCompletionPacket@8:
    movl $0x97, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtFilterToken@24
_NtFilterToken@24:
    movl $0xed, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtImpersonateAnonymousToken@4
_NtImpersonateAnonymousToken@4:
    movl $0x30105, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtSetInformationToken@16
_NtSetInformationToken@16:
    movl $0x1ad, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtOpenProcessToken@12
_NtOpenProcessToken@12:
    movl $0x133, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenProcessTokenEx@16
_NtOpenProcessTokenEx@16:
    movl $0x30, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtOpenSection@12
_NtOpenSection@12:
    movl $0x37, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenSemaphore@12
_NtOpenSemaphore@12:
    movl $0x136, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtMakeTemporaryObject@4
_NtMakeTemporaryObject@4:
    movl $0x30119, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtOpenSymbolicLinkObject@12
_NtOpenSymbolicLinkObject@12:
    movl $0x138, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenThreadToken@16
_NtOpenThreadToken@16:
    movl $0x24, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtOpenThreadTokenEx@20
_NtOpenThreadTokenEx@20:
    movl $0x2f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtOpenTimer@12
_NtOpenTimer@12:
    movl $0x13a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtProtectVirtualMemory@20
_NtProtectVirtualMemory@20:
    movl $0x50, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtPulseEvent@8
_NtPulseEvent@8:
    movl $0x70148, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtQueryAttributesFile@8
_NtQueryAttributesFile@8:
    movl $0x3d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtQueryDebugFilterState@8
_NtQueryDebugFilterState@8:
    movl $0x5014c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtQueryDefaultLocale@8
_NtQueryDefaultLocale@8:
    movl $0x50015, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtQueryDefaultUILanguage@4
_NtQueryDefaultUILanguage@4:
    movl $0x40044, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtQueryDirectoryFile@44
_NtQueryDirectoryFile@44:
    movl $0x35, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $44
    .p2align 4, 0x90
    .globl _NtQueryDirectoryFileEx@40
_NtQueryDirectoryFileEx@40:
    movl $0x14d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtNotifyChangeDirectoryFile@36
_NtNotifyChangeDirectoryFile@36:
    movl $0x121, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtNotifyChangeDirectoryFileEx@40
_NtNotifyChangeDirectoryFileEx@40:
    movl $0x122, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtQueryFullAttributesFile@8
_NtQueryFullAttributesFile@8:
    movl $0x151, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtQueryInformationFile@20
_NtQueryInformationFile@20:
    movl $0x11, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueryInformationJobObject@20
_NtQueryInformationJobObject@20:
    movl $0x156, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueryInformationProcess@20
_NtQueryInformationProcess@20:
    movl $0x19, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueryInformationThread@20
_NtQueryInformationThread@20:
    movl $0x25, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueryInformationToken@20
_NtQueryInformationToken@20:
    movl $0x21, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueryInstallUILanguage@4
_NtQueryInstallUILanguage@4:
    movl $0x4015c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtQueryLicenseValue@20
_NtQueryLicenseValue@20:
    movl $0x160, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueryObject@20
_NtQueryObject@20:
    movl $0x10, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueryPerformanceCounter@8
_NtQueryPerformanceCounter@8:
    movl $0x50031, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtQuerySection@20
_NtQuerySection@20:
    movl $0x51, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQuerySymbolicLinkObject@12
_NtQuerySymbolicLinkObject@12:
    movl $0x16b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtQuerySystemInformation@16
_NtQuerySystemInformation@16:
    movl $0x36, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtQuerySystemEnvironmentValueEx@20
_NtQuerySystemEnvironmentValueEx@20:
    movl $0x16d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtSetSystemEnvironmentValueEx@20
_NtSetSystemEnvironmentValueEx@20:
    movl $0x1bb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtEnumerateSystemEnvironmentValuesEx@12
_NtEnumerateSystemEnvironmentValuesEx@12:
    movl $0xe9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtQuerySystemEnvironmentValue@16
_NtQuerySystemEnvironmentValue@16:
    movl $0x16c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtSetSystemEnvironmentValue@8
_NtSetSystemEnvironmentValue@8:
    movl $0x1ba, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtSetSystemInformation@12
_NtSetSystemInformation@12:
    movl $0x1bc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtShutdownSystem@4
_NtShutdownSystem@4:
    movl $0x401c6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtInitiatePowerAction@16
_NtInitiatePowerAction@16:
    movl $0x11010a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtQuerySystemTime@4
_NtQuerySystemTime@4:
    movl $0x18005a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtQueryTimerResolution@12
_NtQueryTimerResolution@12:
    movl $0xa016f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtQueryValueKey@24
_NtQueryValueKey@24:
    movl $0x17, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtQueryVirtualMemory@24
_NtQueryVirtualMemory@24:
    movl $0x23, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtQueryVolumeInformationFile@20
_NtQueryVolumeInformationFile@20:
    movl $0x49, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtSetVolumeInformationFile@20
_NtSetVolumeInformationFile@20:
    movl $0x1c4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtQueueApcThread@20
_NtQueueApcThread@20:
    movl $0x45, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtRaiseException@12
_NtRaiseException@12:
    movl $0x174, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtLockFile@40
_NtLockFile@40:
    movl $0x114, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtUnlockFile@20
_NtUnlockFile@20:
    movl $0x1dd, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtReadFile@36
_NtReadFile@36:
    movl $0x1a0006, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtReadFileScatter@36
_NtReadFileScatter@36:
    movl $0x1a002e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtWriteFileGather@36
_NtWriteFileGather@36:
    movl $0x1a001b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtReadVirtualMemory@20
_NtReadVirtualMemory@20:
    movl $0x3f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtReleaseMutant@8
_NtReleaseMutant@8:
    movl $0x70020, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtReleaseSemaphore@12
_NtReleaseSemaphore@12:
    movl $0xc000a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtResetEvent@8
_NtResetEvent@8:
    movl $0x70187, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtResumeThread@8
_NtResumeThread@8:
    movl $0x70052, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtSuspendThread@8
_NtSuspendThread@8:
    movl $0x701cf, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtSetEvent@8
_NtSetEvent@8:
    movl $0x7000e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtSetInformationFile@20
_NtSetInformationFile@20:
    movl $0x27, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtSetInformationObject@16
_NtSetInformationObject@16:
    movl $0x5c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtSetInformationProcess@16
_NtSetInformationProcess@16:
    movl $0x1c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtSetInformationThread@16
_NtSetInformationThread@16:
    movl $0xd, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtSetInformationVirtualMemory@24
_NtSetInformationVirtualMemory@24:
    movl $0x1b0, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtSetTimer@28
_NtSetTimer@28:
    movl $0x62, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtSetTimerResolution@12
_NtSetTimerResolution@12:
    movl $0xa01c2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtSignalAndWaitForSingleObject@16
_NtSignalAndWaitForSingleObject@16:
    movl $0x1301c8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtTerminateProcess@8
_NtTerminateProcess@8:
    movl $0x7002c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtCreateJobObject@12
_NtCreateJobObject@12:
    movl $0xb4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtOpenJobObject@12
_NtOpenJobObject@12:
    movl $0x12a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtAssignProcessToJobObject@8
_NtAssignProcessToJobObject@8:
    movl $0x80091, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtTerminateJobObject@8
_NtTerminateJobObject@8:
    movl $0x701d2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtSetInformationJobObject@16
_NtSetInformationJobObject@16:
    movl $0x1a9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtTerminateThread@8
_NtTerminateThread@8:
    movl $0x70053, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtTestAlert@0
_NtTestAlert@0:
    movl $0x201d3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtTraceControl@24
_NtTraceControl@24:
    movl $0x1d6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtTraceEvent@16
_NtTraceEvent@16:
    movl $0x5e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUnmapViewOfSection@8
_NtUnmapViewOfSection@8:
    movl $0x2a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUnmapViewOfSectionEx@12
_NtUnmapViewOfSectionEx@12:
    movl $0x1df, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtWaitForMultipleObjects@20
_NtWaitForMultipleObjects@20:
    movl $0x1d005b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtWaitForSingleObject@12
_NtWaitForSingleObject@12:
    movl $0xd0004, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtWriteFile@36
_NtWriteFile@36:
    movl $0x1a0008, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtWriteVirtualMemory@20
_NtWriteVirtualMemory@20:
    movl $0x3a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtYieldExecution@0
_NtYieldExecution@0:
    movl $0x10046, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtAlertThreadByThreadId@4
_NtAlertThreadByThreadId@4:
    movl $0x40071, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtWaitForAlertByThreadId@8
_NtWaitForAlertByThreadId@8:
    movl $0x601e3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtEnumerateKey@24
_NtEnumerateKey@24:
    movl $0x32, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtEnumerateValueKey@24
_NtEnumerateValueKey@24:
    movl $0x13, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtQueryKey@20
_NtQueryKey@20:
    movl $0x16, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtSetValueKey@24
_NtSetValueKey@24:
    movl $0x60, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtDeleteKey@4
_NtDeleteKey@4:
    movl $0xdc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtDeleteValueKey@8
_NtDeleteValueKey@8:
    movl $0xdf, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtFlushKey@4
_NtFlushKey@4:
    movl $0x300f2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtLoadKey@8
_NtLoadKey@8:
    movl $0x110, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUnloadKey@4
_NtUnloadKey@4:
    movl $0x1da, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtNotifyChangeKey@40
_NtNotifyChangeKey@40:
    movl $0x123, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtNotifyChangeMultipleKeys@48
_NtNotifyChangeMultipleKeys@48:
    movl $0x124, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $48
    .p2align 4, 0x90
    .globl _NtCreateWnfStateName@28
_NtCreateWnfStateName@28:
    movl $0xd4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtDeleteWnfStateName@4
_NtDeleteWnfStateName@4:
    movl $0xe1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUpdateWnfStateData@28
_NtUpdateWnfStateData@28:
    movl $0x1e1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtDeleteWnfStateData@8
_NtDeleteWnfStateData@8:
    movl $0xe0, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtQueryWnfStateData@24
_NtQueryWnfStateData@24:
    movl $0x170, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtQueryWnfStateNameInformation@20
_NtQueryWnfStateNameInformation@20:
    movl $0x171, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtSubscribeWnfStateChange@16
_NtSubscribeWnfStateChange@16:
    movl $0x1cd, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUnsubscribeWnfStateChange@4
_NtUnsubscribeWnfStateChange@4:
    movl $0x1e0, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtSetWnfProcessNotificationEvent@4
_NtSetWnfProcessNotificationEvent@4:
    movl $0x1c5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGetCompleteWnfStateSubscription@24
_NtGetCompleteWnfStateSubscription@24:
    movl $0xfa, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtAddAtom@12
_NtAddAtom@12:
    movl $0xa0047, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtAddAtomEx@16
_NtAddAtomEx@16:
    movl $0x110069, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtFindAtom@12
_NtFindAtom@12:
    movl $0xa0014, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtDeleteAtom@4
_NtDeleteAtom@4:
    movl $0x400d8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtQueryInformationAtom@20
_NtQueryInformationAtom@20:
    movl $0x152, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtCreateUserProcess@44
_NtCreateUserProcess@44:
    movl $0xd1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $44
    .p2align 4, 0x90
    .globl _NtDuplicateToken@24
_NtDuplicateToken@24:
    movl $0x42, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtAccessCheck@32
_NtAccessCheck@32:
    movl $0x0, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $32
    .p2align 4, 0x90
    .globl _NtQuerySecurityObject@20
_NtQuerySecurityObject@20:
    movl $0x168, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtAdjustPrivilegesToken@24
_NtAdjustPrivilegesToken@24:
    movl $0x41, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtQuerySecurityAttributesToken@24
_NtQuerySecurityAttributesToken@24:
    movl $0x167, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtQuerySecurityPolicy@24
_NtQuerySecurityPolicy@24:
    movl $0x169, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
