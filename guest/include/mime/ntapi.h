/*
 * native system service prototypes, shared by user mode and the kernel.
 */
#ifndef MIME_NTAPI_H
#define MIME_NTAPI_H

#include <mime/nt.h>

#ifndef NTSYSCALLAPI
#ifdef MIME_KERNEL
#define NTSYSCALLAPI
#else
#define NTSYSCALLAPI DECLSPEC_IMPORT
#endif
#endif

typedef void(NTAPI *PPS_APC_ROUTINE)(PVOID arg1, PVOID arg2, PVOID arg3);

NTSYSCALLAPI NTSTATUS NTAPI NtAlertThread(HANDLE handle);
NTSYSCALLAPI NTSTATUS NTAPI NtGetWriteWatch(HANDLE process_handle, ULONG flags, PVOID base, SIZE_T size, PVOID *addresses, PULONG_PTR count, PULONG granularity);
NTSYSCALLAPI NTSTATUS NTAPI NtResetWriteWatch(HANDLE process_handle, PVOID base, SIZE_T size);
NTSYSCALLAPI NTSTATUS NTAPI NtAllocateVirtualMemoryEx(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG type, ULONG protect, PMEM_EXTENDED_PARAMETER parameters, ULONG count);
NTSYSCALLAPI NTSTATUS NTAPI NtMapViewOfSectionEx(HANDLE section, HANDLE process, PVOID *base, PLARGE_INTEGER offset, PSIZE_T view_size, ULONG allocation_type, ULONG protect, PMEM_EXTENDED_PARAMETER parameters, ULONG count);
NTSYSCALLAPI NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE process_handle, PVOID *base, ULONG_PTR zero_bits, PSIZE_T size, ULONG type, ULONG protect);
NTSYSCALLAPI NTSTATUS NTAPI NtApphelpCacheControl(ULONG type, PVOID data);
NTSYSCALLAPI NTSTATUS NTAPI NtCallbackReturn(PVOID result, ULONG result_length, NTSTATUS status);
NTSYSCALLAPI NTSTATUS NTAPI NtCancelIoFile(HANDLE handle, PIO_STATUS_BLOCK iosb);
NTSYSCALLAPI NTSTATUS NTAPI NtCancelTimer(HANDLE handle, PBOOLEAN current);
NTSYSCALLAPI NTSTATUS NTAPI NtClearEvent(HANDLE handle);
NTSYSCALLAPI NTSTATUS NTAPI NtClose(HANDLE handle);
NTSYSCALLAPI NTSTATUS NTAPI NtContinue(PCONTEXT context, BOOLEAN test_alert);
NTSYSCALLAPI NTSTATUS NTAPI NtGetContextThread(HANDLE thread, PCONTEXT context);
NTSYSCALLAPI NTSTATUS NTAPI NtCancelIoFileEx(HANDLE handle, PIO_STATUS_BLOCK request, PIO_STATUS_BLOCK iosb);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateNamedPipeFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb, ULONG share, ULONG disposition, ULONG options, ULONG type, ULONG read_mode, ULONG completion_mode, ULONG max_instances, ULONG in_quota, ULONG out_quota, PLARGE_INTEGER timeout);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateIoCompletion(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG concurrency);
NTSYSCALLAPI NTSTATUS NTAPI NtSetIoCompletion(HANDLE handle, PVOID key, PVOID apc_context, NTSTATUS status, ULONG_PTR information);
NTSYSCALLAPI NTSTATUS NTAPI NtRemoveIoCompletion(HANDLE handle, PVOID *key, PVOID *apc_context, PIO_STATUS_BLOCK iosb, PLARGE_INTEGER timeout);
NTSYSCALLAPI NTSTATUS NTAPI NtRemoveIoCompletionEx(HANDLE handle, PVOID entries, ULONG count, PULONG removed, PLARGE_INTEGER timeout, BOOLEAN alertable);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenIoCompletion(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtSetIoCompletionEx(HANDLE handle, HANDLE reserve, PVOID key, PVOID apc_context, NTSTATUS status, ULONG_PTR information);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryIoCompletion(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtSetContextThread(HANDLE thread, PCONTEXT context);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateDirectoryObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateSymbolicLinkObject(PHANDLE handle, ACCESS_MASK access,
                                                        POBJECT_ATTRIBUTES attributes, PUNICODE_STRING target);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateEvent(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, EVENT_TYPE type, BOOLEAN state);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb, PLARGE_INTEGER allocation, ULONG file_attributes, ULONG share, ULONG disposition, ULONG options, PVOID ea, ULONG ea_length);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG index, PUNICODE_STRING klass, ULONG options, PULONG disposition);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateMutant(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, BOOLEAN owner);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PLARGE_INTEGER maximum_size, ULONG protect, ULONG allocation, HANDLE file_handle);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateSemaphore(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, LONG initial, LONG maximum);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateThreadEx(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, HANDLE process_handle, PVOID start, PVOID parameter, ULONG flags, SIZE_T zero_bits, SIZE_T stack_size, SIZE_T max_stack_size, PVOID attribute_list);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateTimer(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, TIMER_TYPE type);
NTSYSCALLAPI NTSTATUS NTAPI NtDelayExecution(BOOLEAN alertable, PLARGE_INTEGER interval);
NTSYSCALLAPI NTSTATUS NTAPI NtDeviceIoControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb, ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len);
NTSYSCALLAPI NTSTATUS NTAPI NtDisplayString(PUNICODE_STRING s);
NTSYSCALLAPI NTSTATUS NTAPI NtDuplicateObject(HANDLE source_process_handle, HANDLE source, HANDLE target_process_handle, PHANDLE target, ACCESS_MASK access, ULONG attributes, ULONG options);
NTSYSCALLAPI NTSTATUS NTAPI NtFlushBuffersFile(HANDLE handle, PIO_STATUS_BLOCK iosb);
NTSYSCALLAPI NTSTATUS NTAPI NtFlushInstructionCache(HANDLE process, PVOID base, SIZE_T size);
NTSYSCALLAPI NTSTATUS NTAPI NtFlushProcessWriteBuffers(void);
NTSYSCALLAPI NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG type);
NTSYSCALLAPI NTSTATUS NTAPI NtFsControlFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb, ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len);
NTSYSCALLAPI ULONG NTAPI NtGetCurrentProcessorNumber(void);
NTSYSCALLAPI ULONG NTAPI NtGetTickCount(void);
NTSYSCALLAPI NTSTATUS NTAPI NtIsProcessInJob(HANDLE process, HANDLE job);
NTSYSCALLAPI NTSTATUS NTAPI NtMapViewOfSection(HANDLE section_handle, HANDLE process_handle, PVOID *base, ULONG_PTR zero_bits, SIZE_T commit_size, PLARGE_INTEGER offset, PSIZE_T view_size, SECTION_INHERIT inherit, ULONG allocation_type, ULONG protect);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenDirectoryObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtPlugPlayControl(ULONG klass, PVOID buffer, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryDirectoryObject(HANDLE handle, PVOID buffer, ULONG length, BOOLEAN single,
                                             BOOLEAN restart, PULONG context, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenEvent(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK iosb, ULONG share, ULONG options);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenKeyEx(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG options);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenMutant(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenProcess(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid);
NTSYSCALLAPI NTSTATUS NTAPI NtSuspendProcess(HANDLE process);
NTSYSCALLAPI NTSTATUS NTAPI NtResumeProcess(HANDLE process);
NTSYSCALLAPI NTSTATUS NTAPI NtSetSecurityObject(HANDLE handle, ULONG info, PVOID descriptor);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenThread(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PCLIENT_ID cid);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateLowBoxToken(PHANDLE new_token, HANDLE token, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PVOID package_sid, ULONG capability_count, PVOID capabilities, ULONG handle_count, PHANDLE handles);
/* access tokens: copies, filters, identities of threads. NtDuplicateToken and NtAdjustPrivilegesToken are advapi32's. */
NTSYSCALLAPI NTSTATUS NTAPI NtAdjustGroupsToken(HANDLE token, BOOLEAN reset_to_default, PVOID state, ULONG length, PVOID previous, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtAllocateLocallyUniqueId(PLUID luid);
NTSYSCALLAPI NTSTATUS NTAPI NtFilterToken(HANDLE token, ULONG flags, PVOID sids_to_disable, PVOID privileges_to_delete, PVOID restricted_sids, PHANDLE new_token);
NTSYSCALLAPI NTSTATUS NTAPI NtImpersonateAnonymousToken(HANDLE thread);
NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationToken(HANDLE token, ULONG klass, PVOID info, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenProcessToken(HANDLE process, ACCESS_MASK access, PHANDLE token);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenProcessTokenEx(HANDLE process, ACCESS_MASK access, ULONG attributes, PHANDLE token);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenSemaphore(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtMakeTemporaryObject(HANDLE handle);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenSymbolicLinkObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenThreadToken(HANDLE thread, ACCESS_MASK access, BOOLEAN self, PHANDLE token);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenThreadTokenEx(HANDLE thread, ACCESS_MASK access, BOOLEAN self, ULONG attributes, PHANDLE token);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenTimer(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE process_handle, PVOID *base, PSIZE_T size, ULONG protect, PULONG old);
NTSYSCALLAPI NTSTATUS NTAPI NtPulseEvent(HANDLE handle, PLONG previous);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryAttributesFile(POBJECT_ATTRIBUTES attributes, PFILE_BASIC_INFORMATION info);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryDebugFilterState(ULONG component, ULONG level);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryDefaultLocale(BOOLEAN user, LCID *locale);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryDefaultUILanguage(LANGID *lang);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass, BOOLEAN single, PUNICODE_STRING pattern, BOOLEAN restart);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryDirectoryFileEx(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass, ULONG flags, PUNICODE_STRING pattern);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryFullAttributesFile(POBJECT_ATTRIBUTES attributes, PFILE_NETWORK_OPEN_INFORMATION info);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationJobObject(HANDLE job, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationToken(HANDLE token, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInstallUILanguage(LANGID *lang);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryLicenseValue(PUNICODE_STRING name, PULONG type, PVOID data, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryObject(HANDLE handle, OBJECT_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryPerformanceCounter(PLARGE_INTEGER counter, PLARGE_INTEGER frequency);
NTSYSCALLAPI NTSTATUS NTAPI NtQuerySection(HANDLE handle, ULONG klass, PVOID info, SIZE_T length, PSIZE_T returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQuerySymbolicLinkObject(HANDLE handle, PUNICODE_STRING target, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtSetSystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtShutdownSystem(SHUTDOWN_ACTION action);
NTSYSCALLAPI NTSTATUS NTAPI NtQuerySystemTime(PLARGE_INTEGER time);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryTimerResolution(PULONG maximum, PULONG minimum, PULONG current);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE process_handle, PVOID base, MEMORY_INFORMATION_CLASS klass, PVOID info, SIZE_T length, PSIZE_T returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FS_INFORMATION_CLASS klass);
NTSYSCALLAPI NTSTATUS NTAPI NtQueueApcThread(HANDLE handle, PPS_APC_ROUTINE routine, PVOID arg1, PVOID arg2, PVOID arg3);
NTSYSCALLAPI NTSTATUS NTAPI NtRaiseException(PEXCEPTION_RECORD record, PCONTEXT context, BOOLEAN first_chance);
NTSYSCALLAPI NTSTATUS NTAPI NtReadFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key);
NTSYSCALLAPI NTSTATUS NTAPI NtReadVirtualMemory(HANDLE process_handle, PVOID base, PVOID buffer, SIZE_T size, PSIZE_T read);
NTSYSCALLAPI NTSTATUS NTAPI NtReleaseMutant(HANDLE handle, PLONG previous);
NTSYSCALLAPI NTSTATUS NTAPI NtReleaseSemaphore(HANDLE handle, LONG count, PLONG previous);
NTSYSCALLAPI NTSTATUS NTAPI NtResetEvent(HANDLE handle, PLONG previous);
NTSYSCALLAPI NTSTATUS NTAPI NtResumeThread(HANDLE handle, PULONG previous);
NTSYSCALLAPI NTSTATUS NTAPI NtSuspendThread(HANDLE handle, PULONG previous);
NTSYSCALLAPI NTSTATUS NTAPI NtSetEvent(HANDLE handle, PLONG previous);
NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length, FILE_INFORMATION_CLASS klass);
NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationObject(HANDLE handle, ULONG klass, PVOID info, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationProcess(HANDLE handle, PROCESSINFOCLASS klass, PVOID info, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationThread(HANDLE handle, THREADINFOCLASS klass, PVOID info, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationVirtualMemory(HANDLE process, ULONG klass, ULONG_PTR count, PVOID entries, PVOID info, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtSetTimer(HANDLE handle, PLARGE_INTEGER due, PVOID apc, PVOID apc_context, BOOLEAN resume, LONG period, PBOOLEAN previous);
NTSYSCALLAPI NTSTATUS NTAPI NtSetTimerResolution(ULONG desired, BOOLEAN set, PULONG current);
NTSYSCALLAPI NTSTATUS NTAPI NtSignalAndWaitForSingleObject(HANDLE signal, HANDLE wait, BOOLEAN alertable, PLARGE_INTEGER timeout);
NTSYSCALLAPI NTSTATUS NTAPI NtTerminateProcess(HANDLE handle, NTSTATUS exit_status);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateJobObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtOpenJobObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSYSCALLAPI NTSTATUS NTAPI NtAssignProcessToJobObject(HANDLE job, HANDLE process);
NTSYSCALLAPI NTSTATUS NTAPI NtTerminateJobObject(HANDLE job, NTSTATUS status);
NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationJobObject(HANDLE job, ULONG klass, PVOID info, ULONG length);
NTSYSCALLAPI NTSTATUS NTAPI NtTerminateThread(HANDLE handle, NTSTATUS exit_status);
NTSYSCALLAPI NTSTATUS NTAPI NtTestAlert(void);
NTSYSCALLAPI NTSTATUS NTAPI NtTraceControl(ULONG code, PVOID in, ULONG in_len, PVOID out, ULONG out_len, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtTraceEvent(HANDLE handle, ULONG flags, ULONG length, PVOID data);
NTSYSCALLAPI NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE process_handle, PVOID base);
NTSYSCALLAPI NTSTATUS NTAPI NtUnmapViewOfSectionEx(HANDLE process_handle, PVOID base, ULONG flags);
NTSYSCALLAPI NTSTATUS NTAPI NtWaitForMultipleObjects(ULONG count, PHANDLE handles, WAIT_TYPE type, BOOLEAN alertable, PLARGE_INTEGER timeout);
NTSYSCALLAPI NTSTATUS NTAPI NtWaitForSingleObject(HANDLE handle, BOOLEAN alertable, PLARGE_INTEGER timeout);
NTSYSCALLAPI NTSTATUS NTAPI NtWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, PLARGE_INTEGER offset, PULONG key);
NTSYSCALLAPI NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE process_handle, PVOID base, PVOID buffer, SIZE_T size, PSIZE_T written);
NTSYSCALLAPI NTSTATUS NTAPI NtYieldExecution(void);
NTSYSCALLAPI NTSTATUS NTAPI NtAlertThreadByThreadId(HANDLE thread_id);
NTSYSCALLAPI NTSTATUS NTAPI NtWaitForAlertByThreadId(PVOID hint, PLARGE_INTEGER timeout);

NTSYSCALLAPI NTSTATUS NTAPI NtEnumerateKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length,
                                           PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtEnumerateValueKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length,
                                                PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryKey(HANDLE key, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtSetValueKey(HANDLE key, PUNICODE_STRING name, ULONG title_index, ULONG type,
                                          PVOID data, ULONG size);
NTSYSCALLAPI NTSTATUS NTAPI NtDeleteKey(HANDLE key);
NTSYSCALLAPI NTSTATUS NTAPI NtDeleteValueKey(HANDLE key, PUNICODE_STRING name);
NTSYSCALLAPI NTSTATUS NTAPI NtFlushKey(HANDLE key);
NTSYSCALLAPI NTSTATUS NTAPI NtLoadKey(POBJECT_ATTRIBUTES target, POBJECT_ATTRIBUTES source);
NTSYSCALLAPI NTSTATUS NTAPI NtUnloadKey(POBJECT_ATTRIBUTES target);
NTSYSCALLAPI NTSTATUS NTAPI NtNotifyChangeKey(HANDLE key, HANDLE event, PVOID apc_routine, PVOID apc_context,
                                              PIO_STATUS_BLOCK iosb, ULONG filter, BOOLEAN subtree, PVOID buffer,
                                              ULONG length, BOOLEAN async);
NTSYSCALLAPI NTSTATUS NTAPI NtNotifyChangeMultipleKeys(HANDLE key, ULONG count, POBJECT_ATTRIBUTES subordinate,
                                                       HANDLE event, PVOID apc_routine, PVOID apc_context,
                                                       PIO_STATUS_BLOCK iosb, ULONG filter, BOOLEAN subtree,
                                                       PVOID buffer, ULONG buffer_length, BOOLEAN async);
NTSYSCALLAPI NTSTATUS NTAPI NtAddAtom(PWSTR name, ULONG length, PUSHORT atom);
NTSYSCALLAPI NTSTATUS NTAPI NtAddAtomEx(PWSTR name, ULONG length, PUSHORT atom, ULONG flags);
NTSYSCALLAPI NTSTATUS NTAPI NtFindAtom(PWSTR name, ULONG length, PUSHORT atom);
NTSYSCALLAPI NTSTATUS NTAPI NtDeleteAtom(USHORT atom);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationAtom(USHORT atom, ULONG klass, PVOID info, ULONG length,
                                                   PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtCreateUserProcess(PHANDLE process, PHANDLE thread, ACCESS_MASK process_access,
    ACCESS_MASK thread_access, POBJECT_ATTRIBUTES process_attributes, POBJECT_ATTRIBUTES thread_attributes,
    ULONG process_flags, ULONG thread_flags, PRTL_USER_PROCESS_PARAMETERS parameters, PVOID create_info,
    PVOID attributes);
#endif
