/*
 * driver development interface of the mime kernel.
 * structures drivers allocate or touch directly keep the 64 bit windows
 * layout, so binary drivers can eventually load unmodified.
 */
#ifndef MIME_DDK_H
#define MIME_DDK_H

#include <mime/nt.h>

#define NTKERNELAPI DECLSPEC_IMPORT
#ifdef MIME_KERNEL
#undef NTKERNELAPI
#define NTKERNELAPI
#endif

#define PASSIVE_LEVEL 0
#define LOW_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define CMCI_LEVEL 5
#define CLOCK_LEVEL 13
#define IPI_LEVEL 14
#define HIGH_LEVEL 15

#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define BYTES_TO_PAGES(n) (((SIZE_T)(n) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define ROUND_TO_PAGES(n) (((ULONG_PTR)(n) + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1))
#define PAGE_ALIGN(va) ((PVOID)((ULONG_PTR)(va) & ~(ULONG_PTR)(PAGE_SIZE - 1)))
#define BYTE_OFFSET(va) ((ULONG)((ULONG_PTR)(va) & (PAGE_SIZE - 1)))

struct _KTHREAD;
struct _KPROCESS;
struct _ETHREAD;
struct _EPROCESS;
struct _DEVICE_OBJECT;
struct _DRIVER_OBJECT;
struct _FILE_OBJECT;
struct _IRP;
struct _KDPC;
struct _KAPC;

typedef struct _KTHREAD *PKTHREAD, *PRKTHREAD;
typedef struct _KPROCESS *PKPROCESS, *PRKPROCESS;
typedef struct _ETHREAD *PETHREAD;
typedef struct _EPROCESS *PEPROCESS;

/* dispatcher objects. */
typedef enum _KOBJECTS {
    EventNotificationObject = 0,
    EventSynchronizationObject = 1,
    MutantObject = 2,
    ProcessObject = 3,
    QueueObject = 4,
    SemaphoreObject = 5,
    ThreadObject = 6,
    GateObject = 7,
    TimerNotificationObject = 8,
    TimerSynchronizationObject = 9,
    ApcObject = 18,
    DpcObject = 19,
} KOBJECTS;

typedef struct _DISPATCHER_HEADER {
    UCHAR Type;
    UCHAR Signalling;
    UCHAR Size;
    UCHAR Reserved1;
    LONG SignalState;
    LIST_ENTRY WaitListHead;
} DISPATCHER_HEADER, *PDISPATCHER_HEADER;
MIME_STATIC_ASSERT(sizeof(DISPATCHER_HEADER) == 0x18, "DISPATCHER_HEADER");

typedef struct _KEVENT {
    DISPATCHER_HEADER Header;
} KEVENT, *PKEVENT, *PRKEVENT;

typedef struct _KSEMAPHORE {
    DISPATCHER_HEADER Header;
    LONG Limit;
} KSEMAPHORE, *PKSEMAPHORE, *PRKSEMAPHORE;
MIME_STATIC_ASSERT(sizeof(KSEMAPHORE) == 0x20, "KSEMAPHORE");

typedef struct _KMUTANT {
    DISPATCHER_HEADER Header;
    LIST_ENTRY MutantListEntry;
    struct _KTHREAD *OwnerThread;
    BOOLEAN Abandoned;
    UCHAR ApcDisable;
} KMUTANT, *PKMUTANT, *PRKMUTANT, KMUTEX, *PKMUTEX, *PRKMUTEX;
MIME_STATIC_ASSERT(sizeof(KMUTANT) == 0x38, "KMUTANT");

typedef void(NTAPI *PKDEFERRED_ROUTINE)(struct _KDPC *Dpc, PVOID DeferredContext, PVOID SystemArgument1,
                                        PVOID SystemArgument2);

typedef struct _KDPC {
    UCHAR Type;
    UCHAR Importance;
    USHORT Number;
    ULONG Inserted;
    SINGLE_LIST_ENTRY DpcListEntry;
    KAFFINITY ProcessorHistory;
    PKDEFERRED_ROUTINE DeferredRoutine;
    PVOID DeferredContext;
    PVOID SystemArgument1;
    PVOID SystemArgument2;
    PVOID DpcData;
} KDPC, *PKDPC, *PRKDPC;
MIME_STATIC_ASSERT(sizeof(KDPC) == 0x40, "KDPC");

typedef struct _KTIMER {
    DISPATCHER_HEADER Header;
    ULARGE_INTEGER DueTime;
    LIST_ENTRY TimerListEntry;
    struct _KDPC *Dpc;
    ULONG Processor;
    LONG Period;
} KTIMER, *PKTIMER, *PRKTIMER;
MIME_STATIC_ASSERT(sizeof(KTIMER) == 0x40, "KTIMER");

typedef enum _KAPC_ENVIRONMENT {
    OriginalApcEnvironment,
    AttachedApcEnvironment,
    CurrentApcEnvironment,
    InsertApcEnvironment
} KAPC_ENVIRONMENT;

typedef void(NTAPI *PKNORMAL_ROUTINE)(PVOID NormalContext, PVOID SystemArgument1, PVOID SystemArgument2);
typedef void(NTAPI *PKKERNEL_ROUTINE)(struct _KAPC *Apc, PKNORMAL_ROUTINE *NormalRoutine, PVOID *NormalContext,
                                      PVOID *SystemArgument1, PVOID *SystemArgument2);
typedef void(NTAPI *PKRUNDOWN_ROUTINE)(struct _KAPC *Apc);

typedef struct _KAPC {
    UCHAR Type;
    UCHAR SpareByte0;
    UCHAR Size;
    UCHAR SpareByte1;
    ULONG SpareLong0;
    struct _KTHREAD *Thread;
    LIST_ENTRY ApcListEntry;
    PKKERNEL_ROUTINE KernelRoutine;
    PKRUNDOWN_ROUTINE RundownRoutine;
    PKNORMAL_ROUTINE NormalRoutine;
    PVOID NormalContext;
    PVOID SystemArgument1;
    PVOID SystemArgument2;
    CCHAR ApcStateIndex;
    KPROCESSOR_MODE ApcMode;
    BOOLEAN Inserted;
} KAPC, *PKAPC, *PRKAPC;
MIME_STATIC_ASSERT(sizeof(KAPC) == 0x58, "KAPC");

typedef enum _KWAIT_REASON {
    Executive = 0,
    FreePage,
    PageIn,
    PoolAllocation,
    DelayExecution,
    Suspended,
    UserRequest,
    WrExecutive,
    WrFreePage,
    WrPageIn,
    WrPoolAllocation,
    WrDelayExecution,
    WrSuspended,
    WrUserRequest,
    WrEventPair,
    WrQueue,
    WrLpcReceive,
    WrLpcReply,
    WrVirtualMemory,
    WrPageOut,
    WrRendezvous,
    WrKeyedEvent,
    WrTerminated,
    WrProcessInSwap,
    WrCpuRateControl,
    WrCalloutStack,
    WrKernel,
    WrResource,
    WrPushLock,
    WrMutex,
    WrQuantumEnd,
    WrDispatchInt,
    WrPreempted,
    WrYieldExecution,
    WrFastMutex,
    WrGuardedMutex,
    WrRundown,
    MaximumWaitReason
} KWAIT_REASON;

typedef struct _FAST_MUTEX {
    LONG Count;
    PVOID Owner;
    ULONG Contention;
    KEVENT Event;
    ULONG OldIrql;
} FAST_MUTEX, *PFAST_MUTEX, KGUARDED_MUTEX, *PKGUARDED_MUTEX;

typedef struct _EX_PUSH_LOCK {
    ULONG_PTR Value;
} EX_PUSH_LOCK, *PEX_PUSH_LOCK;

typedef struct _ERESOURCE {
    LIST_ENTRY SystemResourcesList;
    PVOID OwnerTable;
    SHORT ActiveCount;
    USHORT Flag;
    PKSEMAPHORE SharedWaiters;
    PKEVENT ExclusiveWaiters;
    UCHAR OwnerEntry[16];
    ULONG ActiveEntries;
    ULONG ContentionCount;
    ULONG NumberOfSharedWaiters;
    ULONG NumberOfExclusiveWaiters;
    PVOID Reserved2;
    PVOID Address;
    KSPIN_LOCK SpinLock;
} ERESOURCE, *PERESOURCE;

/* pool. */
typedef enum _POOL_TYPE {
    NonPagedPool = 0,
    NonPagedPoolExecute = 0,
    PagedPool = 1,
    NonPagedPoolMustSucceed = 2,
    NonPagedPoolCacheAligned = 4,
    PagedPoolCacheAligned = 5,
    NonPagedPoolNx = 512,
} POOL_TYPE;

#define POOL_FLAG_UNINITIALIZED 0x0000000000000002ull
#define POOL_FLAG_PAGED 0x0000000000000100ull
#define POOL_FLAG_NON_PAGED 0x0000000000000040ull
#define POOL_FLAG_NON_PAGED_EXECUTE 0x0000000000000080ull
#define POOL_FLAG_RAISE_ON_FAILURE 0x0000000000000020ull

typedef ULONG64 POOL_FLAGS;

/* memory descriptor lists. */
typedef struct _MDL {
    struct _MDL *Next;
    CSHORT Size;
    CSHORT MdlFlags;
    struct _EPROCESS *Process;
    PVOID MappedSystemVa;
    PVOID StartVa;
    ULONG ByteCount;
    ULONG ByteOffset;
} MDL, *PMDL;

#define MDL_MAPPED_TO_SYSTEM_VA 0x0001
#define MDL_PAGES_LOCKED 0x0002
#define MDL_SOURCE_IS_NONPAGED_POOL 0x0004

typedef LARGE_INTEGER PHYSICAL_ADDRESS, *PPHYSICAL_ADDRESS;

/* i/o manager. */
#define IRP_MJ_CREATE 0x00
#define IRP_MJ_CREATE_NAMED_PIPE 0x01
#define IRP_MJ_CLOSE 0x02
#define IRP_MJ_READ 0x03
#define IRP_MJ_WRITE 0x04
#define IRP_MJ_QUERY_INFORMATION 0x05
#define IRP_MJ_SET_INFORMATION 0x06
#define IRP_MJ_QUERY_EA 0x07
#define IRP_MJ_SET_EA 0x08
#define IRP_MJ_FLUSH_BUFFERS 0x09
#define IRP_MJ_QUERY_VOLUME_INFORMATION 0x0a
#define IRP_MJ_SET_VOLUME_INFORMATION 0x0b
#define IRP_MJ_DIRECTORY_CONTROL 0x0c
#define IRP_MJ_FILE_SYSTEM_CONTROL 0x0d
#define IRP_MJ_DEVICE_CONTROL 0x0e
#define IRP_MJ_INTERNAL_DEVICE_CONTROL 0x0f
#define IRP_MJ_SHUTDOWN 0x10
#define IRP_MJ_LOCK_CONTROL 0x11
#define IRP_MJ_CLEANUP 0x12
#define IRP_MJ_CREATE_MAILSLOT 0x13
#define IRP_MJ_QUERY_SECURITY 0x14
#define IRP_MJ_SET_SECURITY 0x15
#define IRP_MJ_POWER 0x16
#define IRP_MJ_SYSTEM_CONTROL 0x17
#define IRP_MJ_DEVICE_CHANGE 0x18
#define IRP_MJ_QUERY_QUOTA 0x19
#define IRP_MJ_SET_QUOTA 0x1a
#define IRP_MJ_PNP 0x1b
#define IRP_MJ_MAXIMUM_FUNCTION 0x1b

#define IRP_MN_QUERY_DIRECTORY 0x01

#define IO_NO_INCREMENT 0
#define IO_DISK_INCREMENT 1
#define IO_KEYBOARD_INCREMENT 6

#define SL_RESTART_SCAN 0x01
#define SL_RETURN_SINGLE_ENTRY 0x02
#define SL_INDEX_SPECIFIED 0x04
#define SL_CASE_SENSITIVE 0x80

#define SL_PENDING_RETURNED 0x01
#define SL_INVOKE_ON_CANCEL 0x20
#define SL_INVOKE_ON_SUCCESS 0x40
#define SL_INVOKE_ON_ERROR 0x80

#define DO_BUFFERED_IO 0x00000004
#define DO_EXCLUSIVE 0x00000008
#define DO_DIRECT_IO 0x00000010
#define DO_DEVICE_INITIALIZING 0x00000080

#define FILE_DEVICE_DISK 0x00000007
#define FILE_DEVICE_DISK_FILE_SYSTEM 0x00000008
#define FILE_DEVICE_NETWORK_FILE_SYSTEM 0x00000014
#define FILE_DEVICE_NULL 0x00000015
#define FILE_DEVICE_UNKNOWN 0x00000022

#define FILE_REMOTE_DEVICE 0x00000010
#define FILE_DEVICE_SECURE_OPEN 0x00000100

#define FO_SYNCHRONOUS_IO 0x00000002
#define FO_ALERTABLE_IO 0x00000004
#define FO_NO_INTERMEDIATE_BUFFERING 0x00000008
#define FO_DIRECTORY_FILE 0x00080000 /* mime private */
#define FO_DELETE_ON_CLOSE 0x00010000
#define FO_SKIP_COMPLETION_PORT 0x02000000
#define FO_SKIP_SET_EVENT 0x04000000
#define FO_HANDLE_CREATED 0x00040000

#define IRP_NOCACHE 0x00000001
#define IRP_PAGING_IO 0x00000002
#define IRP_SYNCHRONOUS_API 0x00000004
#define IRP_BUFFERED_IO 0x00000010
#define IRP_DEALLOCATE_BUFFER 0x00000020
#define IRP_INPUT_OPERATION 0x00000040
#define IRP_CREATE_OPERATION 0x00000080
#define IRP_READ_OPERATION 0x00000100
#define IRP_WRITE_OPERATION 0x00000200
#define IRP_CLOSE_OPERATION 0x00000400

typedef struct _IO_SECURITY_CONTEXT {
    PVOID SecurityQos;
    PVOID AccessState;
    ACCESS_MASK DesiredAccess;
    ULONG FullCreateOptions;
} IO_SECURITY_CONTEXT, *PIO_SECURITY_CONTEXT;

typedef struct _IO_STACK_LOCATION {
    UCHAR MajorFunction;
    UCHAR MinorFunction;
    UCHAR Flags;
    UCHAR Control;
    union {
        struct {
            PIO_SECURITY_CONTEXT SecurityContext;
            ULONG Options; /* disposition << 24 | create options */
            USHORT FileAttributes;
            USHORT ShareAccess;
            ULONG EaLength;
        } Create;
        struct {
            ULONG Length;
            ULONG Key;
            ULONG Flags;
            LARGE_INTEGER ByteOffset;
        } Read;
        struct {
            ULONG Length;
            ULONG Key;
            ULONG Flags;
            LARGE_INTEGER ByteOffset;
        } Write;
        struct {
            ULONG Length;
            PUNICODE_STRING FileName;
            FILE_INFORMATION_CLASS FileInformationClass;
            ULONG FileIndex;
        } QueryDirectory;
        struct {
            ULONG Length;
            FILE_INFORMATION_CLASS FileInformationClass;
        } QueryFile;
        struct {
            ULONG Length;
            FILE_INFORMATION_CLASS FileInformationClass;
            struct _FILE_OBJECT *FileObject;
            union {
                struct {
                    BOOLEAN ReplaceIfExists;
                    BOOLEAN AdvanceOnly;
                };
                ULONG ClusterCount;
                HANDLE DeleteHandle;
            };
        } SetFile;
        struct {
            ULONG Length;
            FS_INFORMATION_CLASS FsInformationClass;
        } QueryVolume;
        struct {
            ULONG OutputBufferLength;
            ULONG InputBufferLength;
            ULONG IoControlCode;
            PVOID Type3InputBuffer;
        } DeviceIoControl;
        struct {
            PVOID Argument1;
            PVOID Argument2;
            PVOID Argument3;
            PVOID Argument4;
        } Others;
    } Parameters;
    struct _DEVICE_OBJECT *DeviceObject;
    struct _FILE_OBJECT *FileObject;
    NTSTATUS(NTAPI *CompletionRoutine)(struct _DEVICE_OBJECT *, struct _IRP *, PVOID);
    PVOID Context;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
MIME_FIELD_AT(IO_STACK_LOCATION, Parameters, 0x8);
MIME_FIELD_AT(IO_STACK_LOCATION, DeviceObject, 0x28);
MIME_FIELD_AT(IO_STACK_LOCATION, FileObject, 0x30);
MIME_STATIC_ASSERT(sizeof(IO_STACK_LOCATION) == 0x48, "IO_STACK_LOCATION");

typedef NTSTATUS(NTAPI *PIO_COMPLETION_ROUTINE)(struct _DEVICE_OBJECT *DeviceObject, struct _IRP *Irp,
                                               PVOID Context);
typedef void(NTAPI *PDRIVER_CANCEL)(struct _DEVICE_OBJECT *DeviceObject, struct _IRP *Irp);

typedef struct _KDEVICE_QUEUE_ENTRY {
    LIST_ENTRY DeviceListEntry;
    ULONG SortKey;
    BOOLEAN Inserted;
} KDEVICE_QUEUE_ENTRY, *PKDEVICE_QUEUE_ENTRY;

typedef struct _IRP {
    CSHORT Type;
    USHORT Size;
    PMDL MdlAddress;
    ULONG Flags;
    union {
        struct _IRP *MasterIrp;
        LONG IrpCount;
        PVOID SystemBuffer;
    } AssociatedIrp;
    LIST_ENTRY ThreadListEntry;
    IO_STATUS_BLOCK IoStatus;
    KPROCESSOR_MODE RequestorMode;
    BOOLEAN PendingReturned;
    CHAR StackCount;
    CHAR CurrentLocation;
    BOOLEAN Cancel;
    KIRQL CancelIrql;
    CCHAR ApcEnvironment;
    UCHAR AllocationFlags;
    PIO_STATUS_BLOCK UserIosb;
    PKEVENT UserEvent;
    union {
        struct {
            PIO_APC_ROUTINE UserApcRoutine;
            PVOID UserApcContext;
        } AsynchronousParameters;
        LARGE_INTEGER AllocationSize;
    } Overlay;
    PDRIVER_CANCEL CancelRoutine;
    PVOID UserBuffer;
    union {
        struct {
            union {
                KDEVICE_QUEUE_ENTRY DeviceQueueEntry;
                PVOID DriverContext[4];
            };
            PETHREAD Thread;
            PCHAR AuxiliaryBuffer;
            LIST_ENTRY ListEntry;
            union {
                struct _IO_STACK_LOCATION *CurrentStackLocation;
                ULONG PacketType;
            };
            struct _FILE_OBJECT *OriginalFileObject;
        } Overlay;
        KAPC Apc;
        PVOID CompletionKey;
    } Tail;
} IRP, *PIRP;
MIME_FIELD_AT(IRP, IoStatus, 0x30);
MIME_FIELD_AT(IRP, UserIosb, 0x48);
MIME_FIELD_AT(IRP, UserBuffer, 0x70);
MIME_FIELD_AT(IRP, Tail.Overlay.Thread, 0x98);
MIME_FIELD_AT(IRP, Tail.Overlay.CurrentStackLocation, 0xB8);
MIME_STATIC_ASSERT(sizeof(IRP) == 0xD0, "IRP");

typedef NTSTATUS(NTAPI *PDRIVER_DISPATCH)(struct _DEVICE_OBJECT *DeviceObject, struct _IRP *Irp);
typedef NTSTATUS(NTAPI *PDRIVER_INITIALIZE)(struct _DRIVER_OBJECT *DriverObject, PUNICODE_STRING RegistryPath);
typedef void(NTAPI *PDRIVER_UNLOAD)(struct _DRIVER_OBJECT *DriverObject);
typedef void(NTAPI *PDRIVER_STARTIO)(struct _DEVICE_OBJECT *DeviceObject, struct _IRP *Irp);
typedef NTSTATUS(NTAPI *PDRIVER_ADD_DEVICE)(struct _DRIVER_OBJECT *DriverObject,
                                           struct _DEVICE_OBJECT *PhysicalDeviceObject);

typedef struct _DRIVER_EXTENSION {
    struct _DRIVER_OBJECT *DriverObject;
    PDRIVER_ADD_DEVICE AddDevice;
    ULONG Count;
    UNICODE_STRING ServiceKeyName;
} DRIVER_EXTENSION, *PDRIVER_EXTENSION;

typedef struct _DRIVER_OBJECT {
    CSHORT Type;
    CSHORT Size;
    struct _DEVICE_OBJECT *DeviceObject;
    ULONG Flags;
    PVOID DriverStart;
    ULONG DriverSize;
    PVOID DriverSection;
    PDRIVER_EXTENSION DriverExtension;
    UNICODE_STRING DriverName;
    PUNICODE_STRING HardwareDatabase;
    PVOID FastIoDispatch;
    PDRIVER_INITIALIZE DriverInit;
    PDRIVER_STARTIO DriverStartIo;
    PDRIVER_UNLOAD DriverUnload;
    PDRIVER_DISPATCH MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
} DRIVER_OBJECT, *PDRIVER_OBJECT;
MIME_FIELD_AT(DRIVER_OBJECT, DriverName, 0x38);
MIME_FIELD_AT(DRIVER_OBJECT, DriverInit, 0x58);
MIME_FIELD_AT(DRIVER_OBJECT, MajorFunction, 0x70);
MIME_STATIC_ASSERT(sizeof(DRIVER_OBJECT) == 0x150, "DRIVER_OBJECT");

typedef struct _VPB {
    CSHORT Type;
    CSHORT Size;
    USHORT Flags;
    USHORT VolumeLabelLength;
    struct _DEVICE_OBJECT *DeviceObject;
    struct _DEVICE_OBJECT *RealDevice;
    ULONG SerialNumber;
    ULONG ReferenceCount;
    WCHAR VolumeLabel[32];
} VPB, *PVPB;

typedef struct DECLSPEC_ALIGN(16) _DEVICE_OBJECT {
    CSHORT Type;
    USHORT Size;
    LONG ReferenceCount;
    struct _DRIVER_OBJECT *DriverObject;
    struct _DEVICE_OBJECT *NextDevice;
    struct _DEVICE_OBJECT *AttachedDevice;
    struct _IRP *CurrentIrp;
    PVOID Timer;
    ULONG Flags;
    ULONG Characteristics;
    PVPB Vpb;
    PVOID DeviceExtension;
    ULONG DeviceType;
    CCHAR StackSize;
    ULONG64 QueueReserved[9];
    ULONG AlignmentRequirement;
    ULONG64 DeviceQueue[5];
    KDPC Dpc;
    ULONG ActiveThreadCount;
    PVOID SecurityDescriptor;
    KEVENT DeviceLock;
    USHORT SectorSize;
    USHORT Spare1;
    PVOID DeviceObjectExtension;
    PVOID Reserved;
} DEVICE_OBJECT, *PDEVICE_OBJECT;
MIME_FIELD_AT(DEVICE_OBJECT, DeviceExtension, 0x40);
MIME_FIELD_AT(DEVICE_OBJECT, DeviceType, 0x48);
MIME_FIELD_AT(DEVICE_OBJECT, Dpc, 0xC8);
MIME_FIELD_AT(DEVICE_OBJECT, DeviceLock, 0x118);
MIME_STATIC_ASSERT(sizeof(DEVICE_OBJECT) == 0x150, "DEVICE_OBJECT");

typedef struct _SECTION_OBJECT_POINTERS {
    PVOID DataSectionObject;
    PVOID SharedCacheMap;
    PVOID ImageSectionObject;
} SECTION_OBJECT_POINTERS, *PSECTION_OBJECT_POINTERS;

typedef struct _FILE_OBJECT {
    CSHORT Type;
    CSHORT Size;
    PDEVICE_OBJECT DeviceObject;
    PVPB Vpb;
    PVOID FsContext;
    PVOID FsContext2;
    PSECTION_OBJECT_POINTERS SectionObjectPointer;
    PVOID PrivateCacheMap;
    NTSTATUS FinalStatus;
    struct _FILE_OBJECT *RelatedFileObject;
    BOOLEAN LockOperation;
    BOOLEAN DeletePending;
    BOOLEAN ReadAccess;
    BOOLEAN WriteAccess;
    BOOLEAN DeleteAccess;
    BOOLEAN SharedRead;
    BOOLEAN SharedWrite;
    BOOLEAN SharedDelete;
    ULONG Flags;
    UNICODE_STRING FileName;
    LARGE_INTEGER CurrentByteOffset;
    ULONG Waiters;
    ULONG Busy;
    PVOID LastLock;
    KEVENT Lock;
    KEVENT Event;
    PVOID CompletionContext;
    KSPIN_LOCK IrpListLock;
    LIST_ENTRY IrpList;
    PVOID FileObjectExtension;
} FILE_OBJECT, *PFILE_OBJECT;
MIME_FIELD_AT(FILE_OBJECT, FsContext, 0x18);
MIME_FIELD_AT(FILE_OBJECT, Flags, 0x50);
MIME_FIELD_AT(FILE_OBJECT, FileName, 0x58);
MIME_FIELD_AT(FILE_OBJECT, CurrentByteOffset, 0x68);
MIME_FIELD_AT(FILE_OBJECT, Event, 0x98);
MIME_STATIC_ASSERT(sizeof(FILE_OBJECT) == 0xD8, "FILE_OBJECT");

#define IO_TYPE_DRIVER 4
#define IO_TYPE_DEVICE 3
#define IO_TYPE_FILE 5
#define IO_TYPE_IRP 6

FORCEINLINE PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP irp)
{
    return irp->Tail.Overlay.CurrentStackLocation;
}

FORCEINLINE PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP irp)
{
    return irp->Tail.Overlay.CurrentStackLocation - 1;
}

FORCEINLINE void IoSkipCurrentIrpStackLocation(PIRP irp)
{
    irp->CurrentLocation++;
    irp->Tail.Overlay.CurrentStackLocation++;
}

FORCEINLINE void IoCopyCurrentIrpStackLocationToNext(PIRP irp)
{
    PIO_STACK_LOCATION cur = IoGetCurrentIrpStackLocation(irp);
    PIO_STACK_LOCATION next = IoGetNextIrpStackLocation(irp);
    *next = *cur;
    next->CompletionRoutine = nullptr;
    next->Context = nullptr;
    next->Control = 0;
}

FORCEINLINE void IoMarkIrpPending(PIRP irp)
{
    IoGetCurrentIrpStackLocation(irp)->Control |= SL_PENDING_RETURNED;
}

FORCEINLINE void IoSetCompletionRoutine(PIRP irp, PIO_COMPLETION_ROUTINE routine, PVOID context,
                                        BOOLEAN on_success, BOOLEAN on_error, BOOLEAN on_cancel)
{
    PIO_STACK_LOCATION next = IoGetNextIrpStackLocation(irp);
    next->CompletionRoutine = routine;
    next->Context = context;
    next->Control = 0;
    if (on_success)
        next->Control |= SL_INVOKE_ON_SUCCESS;
    if (on_error)
        next->Control |= SL_INVOKE_ON_ERROR;
    if (on_cancel)
        next->Control |= SL_INVOKE_ON_CANCEL;
}

typedef void(NTAPI *PKSTART_ROUTINE)(PVOID StartContext);

/* system service tables, registered by win32k with KeAddSystemServiceTable. */
typedef struct _KSERVICE_DESCRIPTOR {
    PVOID Routine;
    UCHAR StackArgs; /* arguments beyond the eight passed in registers */
    UCHAR Implemented;
    USHORT Reserved;
} KSERVICE_DESCRIPTOR, *PKSERVICE_DESCRIPTOR;

/* exported kernel interface. */
NTKERNELAPI ULONG DbgPrint(PCSTR format, ...);
NTKERNELAPI ULONG vDbgPrintEx(ULONG component, ULONG level, PCSTR format, va_list ap);
NTKERNELAPI DECLSPEC_NORETURN void NTAPI KeBugCheckEx(ULONG code, ULONG_PTR p1, ULONG_PTR p2, ULONG_PTR p3,
                                                      ULONG_PTR p4);
NTKERNELAPI DECLSPEC_NORETURN void NTAPI KeBugCheck(ULONG code);

NTKERNELAPI KIRQL NTAPI KeGetCurrentIrql(void);
NTKERNELAPI KIRQL NTAPI KfRaiseIrql(KIRQL irql);
NTKERNELAPI void NTAPI KeLowerIrql(KIRQL irql);
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToDpcLevel(void);
#define KeRaiseIrql(a, b) (*(b) = KfRaiseIrql(a))
NTKERNELAPI KIRQL NTAPI KeAcquireSpinLockRaiseToDpc(PKSPIN_LOCK lock);
NTKERNELAPI void NTAPI KeReleaseSpinLock(PKSPIN_LOCK lock, KIRQL irql);
#define KeAcquireSpinLock(l, i) (*(i) = KeAcquireSpinLockRaiseToDpc(l))
NTKERNELAPI void NTAPI KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK lock);
NTKERNELAPI void NTAPI KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK lock);
FORCEINLINE void KeInitializeSpinLock(PKSPIN_LOCK lock)
{
    *lock = 0;
}

NTKERNELAPI PKTHREAD NTAPI KeGetCurrentThread(void);
NTKERNELAPI PEPROCESS NTAPI PsGetCurrentProcess(void);
NTKERNELAPI PETHREAD NTAPI PsGetCurrentThread(void);
NTKERNELAPI HANDLE NTAPI PsGetCurrentProcessId(void);
NTKERNELAPI HANDLE NTAPI PsGetCurrentThreadId(void);
NTKERNELAPI KPROCESSOR_MODE NTAPI ExGetPreviousMode(void);
NTKERNELAPI KPROCESSOR_MODE NTAPI KeGetPreviousMode(void);
NTKERNELAPI ULONG NTAPI KeGetCurrentProcessorNumber(void);

NTKERNELAPI void NTAPI KeInitializeEvent(PRKEVENT event, EVENT_TYPE type, BOOLEAN state);
NTKERNELAPI LONG NTAPI KeSetEvent(PRKEVENT event, KPRIORITY increment, BOOLEAN wait);
NTKERNELAPI LONG NTAPI KeResetEvent(PRKEVENT event);
NTKERNELAPI void NTAPI KeClearEvent(PRKEVENT event);
NTKERNELAPI LONG NTAPI KeReadStateEvent(PRKEVENT event);
NTKERNELAPI LONG NTAPI KePulseEvent(PRKEVENT event, KPRIORITY increment, BOOLEAN wait);
NTKERNELAPI void NTAPI KeInitializeSemaphore(PRKSEMAPHORE sem, LONG count, LONG limit);
NTKERNELAPI LONG NTAPI KeReleaseSemaphore(PRKSEMAPHORE sem, KPRIORITY increment, LONG adjustment, BOOLEAN wait);
NTKERNELAPI void NTAPI KeInitializeMutex(PRKMUTEX mutex, ULONG level);
NTKERNELAPI LONG NTAPI KeReleaseMutex(PRKMUTEX mutex, BOOLEAN wait);
NTKERNELAPI void NTAPI KeInitializeMutant(PRKMUTANT mutant, BOOLEAN initial_owner);
NTKERNELAPI LONG NTAPI KeReleaseMutant(PRKMUTANT mutant, KPRIORITY increment, BOOLEAN abandoned, BOOLEAN wait);
NTKERNELAPI void NTAPI KeInitializeTimer(PKTIMER timer);
NTKERNELAPI void NTAPI KeInitializeTimerEx(PKTIMER timer, TIMER_TYPE type);
NTKERNELAPI BOOLEAN NTAPI KeSetTimer(PKTIMER timer, LARGE_INTEGER due, PKDPC dpc);
NTKERNELAPI BOOLEAN NTAPI KeSetTimerEx(PKTIMER timer, LARGE_INTEGER due, LONG period, PKDPC dpc);
NTKERNELAPI BOOLEAN NTAPI KeCancelTimer(PKTIMER timer);
NTKERNELAPI BOOLEAN NTAPI KeReadStateTimer(PKTIMER timer);
NTKERNELAPI void NTAPI KeInitializeDpc(PRKDPC dpc, PKDEFERRED_ROUTINE routine, PVOID context);
NTKERNELAPI BOOLEAN NTAPI KeInsertQueueDpc(PRKDPC dpc, PVOID arg1, PVOID arg2);
NTKERNELAPI BOOLEAN NTAPI KeRemoveQueueDpc(PRKDPC dpc);
NTKERNELAPI NTSTATUS NTAPI KeWaitForSingleObject(PVOID object, KWAIT_REASON reason, KPROCESSOR_MODE mode,
                                                 BOOLEAN alertable, PLARGE_INTEGER timeout);
NTKERNELAPI NTSTATUS NTAPI KeWaitForMultipleObjects(ULONG count, PVOID objects[], WAIT_TYPE type,
                                                    KWAIT_REASON reason, KPROCESSOR_MODE mode,
                                                    BOOLEAN alertable, PLARGE_INTEGER timeout, PVOID wait_blocks);
#define KeWaitForMutexObject KeWaitForSingleObject
NTKERNELAPI NTSTATUS NTAPI KeDelayExecutionThread(KPROCESSOR_MODE mode, BOOLEAN alertable, PLARGE_INTEGER interval);
NTKERNELAPI void NTAPI KeQuerySystemTime(PLARGE_INTEGER time);
NTKERNELAPI void NTAPI KeQueryTickCount(PLARGE_INTEGER count);
NTKERNELAPI ULONGLONG NTAPI KeQueryInterruptTime(void);
NTKERNELAPI LARGE_INTEGER NTAPI KeQueryPerformanceCounter(PLARGE_INTEGER frequency);
NTKERNELAPI ULONG NTAPI KeQueryTimeIncrement(void);
NTKERNELAPI void NTAPI KeEnterCriticalRegion(void);
NTKERNELAPI void NTAPI KeLeaveCriticalRegion(void);
NTKERNELAPI void NTAPI KeEnterGuardedRegion(void);
NTKERNELAPI void NTAPI KeLeaveGuardedRegion(void);
NTKERNELAPI void NTAPI KeInitializeGuardedMutex(PKGUARDED_MUTEX mutex);
NTKERNELAPI void NTAPI KeAcquireGuardedMutex(PKGUARDED_MUTEX mutex);
NTKERNELAPI void NTAPI KeReleaseGuardedMutex(PKGUARDED_MUTEX mutex);
NTKERNELAPI BOOLEAN NTAPI KeAddSystemServiceTable(PKSERVICE_DESCRIPTOR table, PVOID unused, ULONG count,
                                                  PVOID arguments, ULONG index);
NTKERNELAPI NTSTATUS NTAPI KeUserModeCallback(ULONG api, PVOID input, ULONG input_length, PVOID *output,
                                              PULONG output_length);
NTKERNELAPI void NTAPI KeStackAttachProcess(PRKPROCESS process, PVOID state);
NTKERNELAPI void NTAPI KeUnstackDetachProcess(PVOID state);
NTKERNELAPI void NTAPI ExInitializeFastMutex(PFAST_MUTEX mutex);
NTKERNELAPI void FASTCALL ExAcquireFastMutex(PFAST_MUTEX mutex);
NTKERNELAPI void FASTCALL ExReleaseFastMutex(PFAST_MUTEX mutex);
NTKERNELAPI void FASTCALL ExAcquireFastMutexUnsafe(PFAST_MUTEX mutex);
NTKERNELAPI void FASTCALL ExReleaseFastMutexUnsafe(PFAST_MUTEX mutex);
NTKERNELAPI void NTAPI ExInitializePushLock(PEX_PUSH_LOCK lock);
NTKERNELAPI void FASTCALL ExfAcquirePushLockExclusive(PEX_PUSH_LOCK lock);
NTKERNELAPI void FASTCALL ExfReleasePushLockExclusive(PEX_PUSH_LOCK lock);
NTKERNELAPI void FASTCALL ExfAcquirePushLockShared(PEX_PUSH_LOCK lock);
NTKERNELAPI void FASTCALL ExfReleasePushLockShared(PEX_PUSH_LOCK lock);
NTKERNELAPI NTSTATUS NTAPI ExInitializeResourceLite(PERESOURCE resource);
NTKERNELAPI NTSTATUS NTAPI ExDeleteResourceLite(PERESOURCE resource);
NTKERNELAPI BOOLEAN NTAPI ExAcquireResourceExclusiveLite(PERESOURCE resource, BOOLEAN wait);
NTKERNELAPI BOOLEAN NTAPI ExAcquireResourceSharedLite(PERESOURCE resource, BOOLEAN wait);
NTKERNELAPI void FASTCALL ExReleaseResourceLite(PERESOURCE resource);

NTKERNELAPI PVOID NTAPI ExAllocatePool2(POOL_FLAGS flags, SIZE_T size, ULONG tag);
NTKERNELAPI PVOID NTAPI ExAllocatePoolWithTag(POOL_TYPE type, SIZE_T size, ULONG tag);
NTKERNELAPI PVOID NTAPI ExAllocatePool(POOL_TYPE type, SIZE_T size);
NTKERNELAPI PVOID NTAPI ExAllocatePoolZero(POOL_TYPE type, SIZE_T size, ULONG tag);
NTKERNELAPI void NTAPI ExFreePoolWithTag(PVOID p, ULONG tag);
NTKERNELAPI void NTAPI ExFreePool(PVOID p);
NTKERNELAPI LARGE_INTEGER NTAPI MmGetPhysicalAddress(PVOID va);
NTKERNELAPI PVOID NTAPI MmAllocateContiguousMemory(SIZE_T size, PHYSICAL_ADDRESS highest);
NTKERNELAPI void NTAPI MmFreeContiguousMemory(PVOID va);
NTKERNELAPI PVOID NTAPI MmGetSystemRoutineAddress(PUNICODE_STRING name);
NTKERNELAPI BOOLEAN NTAPI MmIsAddressValid(PVOID va);
NTKERNELAPI NTSTATUS NTAPI MmMapViewInSystemSpace(PVOID section, PVOID *base, PSIZE_T size);
NTKERNELAPI NTSTATUS NTAPI MmUnmapViewInSystemSpace(PVOID base);
NTKERNELAPI NTSTATUS NTAPI MmCopyVirtualMemory(PEPROCESS from_process, PVOID from, PEPROCESS to_process, PVOID to,
                                               SIZE_T size, KPROCESSOR_MODE mode, PSIZE_T copied);
NTKERNELAPI void NTAPI ProbeForRead(PVOID address, SIZE_T length, ULONG alignment);
NTKERNELAPI void NTAPI ProbeForWrite(PVOID address, SIZE_T length, ULONG alignment);

NTKERNELAPI NTSTATUS NTAPI PsCreateSystemThread(PHANDLE handle, ULONG access, POBJECT_ATTRIBUTES attributes,
                                                HANDLE process, PCLIENT_ID client, PKSTART_ROUTINE routine,
                                                PVOID context);
NTKERNELAPI NTSTATUS NTAPI PsTerminateSystemThread(NTSTATUS status);
NTKERNELAPI NTSTATUS NTAPI PsLookupProcessByProcessId(HANDLE id, PEPROCESS *process);
NTKERNELAPI NTSTATUS NTAPI PsLookupThreadByThreadId(HANDLE id, PETHREAD *thread);
NTKERNELAPI HANDLE NTAPI PsGetProcessId(PEPROCESS process);
NTKERNELAPI HANDLE NTAPI PsGetThreadId(PETHREAD thread);
NTKERNELAPI HANDLE NTAPI PsGetThreadProcessId(PETHREAD thread);
NTKERNELAPI PEPROCESS NTAPI PsGetThreadProcess(PETHREAD thread);
NTKERNELAPI PVOID NTAPI PsGetProcessWin32Process(PEPROCESS process);
NTKERNELAPI NTSTATUS NTAPI PsSetProcessWin32Process(PEPROCESS process, PVOID win32, PVOID old_win32);
NTKERNELAPI PVOID NTAPI PsGetThreadWin32Thread(PETHREAD thread);
NTKERNELAPI PVOID NTAPI PsGetCurrentThreadWin32Thread(void);
NTKERNELAPI PVOID NTAPI PsSetThreadWin32Thread(PETHREAD thread, PVOID win32, PVOID old_win32);
NTKERNELAPI PVOID NTAPI PsGetProcessPeb(PEPROCESS process);
NTKERNELAPI PVOID NTAPI PsGetThreadTeb(PETHREAD thread);
NTKERNELAPI PVOID NTAPI PsGetCurrentThreadTeb(void);
extern NTKERNELAPI PEPROCESS PsInitialSystemProcess;
NTKERNELAPI BOOLEAN NTAPI PsIsThreadTerminating(PETHREAD thread);
NTKERNELAPI NTSTATUS NTAPI PsGetProcessExitStatus(PEPROCESS process);

typedef void(NTAPI *PCREATE_PROCESS_NOTIFY_ROUTINE)(HANDLE parent, HANDLE process, BOOLEAN create);
typedef void(NTAPI *PCREATE_THREAD_NOTIFY_ROUTINE)(HANDLE process, HANDLE thread, BOOLEAN create);
NTKERNELAPI NTSTATUS NTAPI PsSetCreateProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE routine, BOOLEAN remove);
NTKERNELAPI NTSTATUS NTAPI PsSetCreateThreadNotifyRoutine(PCREATE_THREAD_NOTIFY_ROUTINE routine);

typedef struct _OBJECT_TYPE *POBJECT_TYPE;
extern NTKERNELAPI POBJECT_TYPE *PsProcessType;
extern NTKERNELAPI POBJECT_TYPE *PsThreadType;
extern NTKERNELAPI POBJECT_TYPE *ExEventObjectType;
extern NTKERNELAPI POBJECT_TYPE *ExSemaphoreObjectType;
extern NTKERNELAPI POBJECT_TYPE *IoFileObjectType;
extern NTKERNELAPI POBJECT_TYPE *MmSectionObjectType;

NTKERNELAPI NTSTATUS NTAPI ObReferenceObjectByHandle(HANDLE handle, ACCESS_MASK access, POBJECT_TYPE type,
                                                     KPROCESSOR_MODE mode, PVOID *object, PVOID info);
NTKERNELAPI NTSTATUS NTAPI ObReferenceObjectByPointer(PVOID object, ACCESS_MASK access, POBJECT_TYPE type,
                                                      KPROCESSOR_MODE mode);
NTKERNELAPI LONG_PTR FASTCALL ObfReferenceObject(PVOID object);
NTKERNELAPI LONG_PTR FASTCALL ObfDereferenceObject(PVOID object);
#define ObReferenceObject(o) ObfReferenceObject(o)
#define ObDereferenceObject(o) ObfDereferenceObject(o)
NTKERNELAPI void NTAPI ObDereferenceObjectDeferDelete(PVOID object);
NTKERNELAPI NTSTATUS NTAPI ObOpenObjectByPointer(PVOID object, ULONG attributes, PVOID access_state,
                                                 ACCESS_MASK access, POBJECT_TYPE type, KPROCESSOR_MODE mode,
                                                 PHANDLE handle);
NTKERNELAPI NTSTATUS NTAPI ObReferenceObjectByName(PUNICODE_STRING name, ULONG attributes, PVOID access_state,
                                                   ACCESS_MASK access, POBJECT_TYPE type, KPROCESSOR_MODE mode,
                                                   PVOID parse_context, PVOID *object);

NTKERNELAPI NTSTATUS NTAPI IoCreateDevice(PDRIVER_OBJECT driver, ULONG extension_size, PUNICODE_STRING name,
                                          ULONG type, ULONG characteristics, BOOLEAN exclusive,
                                          PDEVICE_OBJECT *device);
NTKERNELAPI void NTAPI IoDeleteDevice(PDEVICE_OBJECT device);
NTKERNELAPI NTSTATUS NTAPI IoCreateSymbolicLink(PUNICODE_STRING link, PUNICODE_STRING target);
NTKERNELAPI NTSTATUS NTAPI IoDeleteSymbolicLink(PUNICODE_STRING link);
NTKERNELAPI NTSTATUS FASTCALL IofCallDriver(PDEVICE_OBJECT device, PIRP irp);
NTKERNELAPI void FASTCALL IofCompleteRequest(PIRP irp, CCHAR boost);
#define IoCallDriver(d, i) IofCallDriver(d, i)
#define IoCompleteRequest(i, b) IofCompleteRequest(i, b)
NTKERNELAPI PIRP NTAPI IoAllocateIrp(CCHAR stack_size, BOOLEAN charge_quota);
NTKERNELAPI void NTAPI IoFreeIrp(PIRP irp);
NTKERNELAPI PIRP NTAPI IoBuildSynchronousFsdRequest(ULONG major, PDEVICE_OBJECT device, PVOID buffer, ULONG length,
                                                    PLARGE_INTEGER offset, PKEVENT event, PIO_STATUS_BLOCK iosb);
NTKERNELAPI PIRP NTAPI IoBuildDeviceIoControlRequest(ULONG code, PDEVICE_OBJECT device, PVOID input, ULONG in_len,
                                                     PVOID output, ULONG out_len, BOOLEAN internal, PKEVENT event,
                                                     PIO_STATUS_BLOCK iosb);
NTKERNELAPI PDEVICE_OBJECT NTAPI IoGetRelatedDeviceObject(PFILE_OBJECT file);
NTKERNELAPI PDEVICE_OBJECT NTAPI IoGetAttachedDevice(PDEVICE_OBJECT device);
NTKERNELAPI PDEVICE_OBJECT NTAPI IoAttachDeviceToDeviceStack(PDEVICE_OBJECT source, PDEVICE_OBJECT target);
NTKERNELAPI void NTAPI IoDetachDevice(PDEVICE_OBJECT target);
NTKERNELAPI NTSTATUS NTAPI IoGetDeviceObjectPointer(PUNICODE_STRING name, ACCESS_MASK access, PFILE_OBJECT *file,
                                                    PDEVICE_OBJECT *device);
NTKERNELAPI PMDL NTAPI IoAllocateMdl(PVOID va, ULONG length, BOOLEAN secondary, BOOLEAN charge, PIRP irp);
NTKERNELAPI void NTAPI IoFreeMdl(PMDL mdl);
NTKERNELAPI NTSTATUS NTAPI IoCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                        PIO_STATUS_BLOCK iosb, PLARGE_INTEGER allocation, ULONG file_attributes,
                                        ULONG share, ULONG disposition, ULONG options, PVOID ea, ULONG ea_length,
                                        ULONG create_file_type, PVOID parameters, ULONG flags);

/* zw routines callable from kernel mode drivers. */
NTKERNELAPI NTSTATUS NTAPI ZwClose(HANDLE handle);
NTKERNELAPI NTSTATUS NTAPI ZwQueryDirectoryFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                                PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                                FILE_INFORMATION_CLASS klass, BOOLEAN single, PUNICODE_STRING pattern,
                                                BOOLEAN restart);
NTKERNELAPI NTSTATUS NTAPI ZwCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                        PIO_STATUS_BLOCK iosb, PLARGE_INTEGER allocation, ULONG file_attributes,
                                        ULONG share, ULONG disposition, ULONG options, PVOID ea, ULONG ea_length);
NTKERNELAPI NTSTATUS NTAPI ZwOpenFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                      PIO_STATUS_BLOCK iosb, ULONG share, ULONG options);
NTKERNELAPI NTSTATUS NTAPI ZwReadFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                      PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, PLARGE_INTEGER offset,
                                      PULONG key);
NTKERNELAPI NTSTATUS NTAPI ZwWriteFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                       PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, PLARGE_INTEGER offset,
                                       PULONG key);
NTKERNELAPI NTSTATUS NTAPI ZwQueryInformationFile(HANDLE handle, PIO_STATUS_BLOCK iosb, PVOID info, ULONG length,
                                                  FILE_INFORMATION_CLASS klass);
NTKERNELAPI NTSTATUS NTAPI ZwCreateSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                           PLARGE_INTEGER size, ULONG protect, ULONG allocation, HANDLE file);
NTKERNELAPI NTSTATUS NTAPI ZwMapViewOfSection(HANDLE section, HANDLE process, PVOID *base, ULONG_PTR zero_bits,
                                              SIZE_T commit, PLARGE_INTEGER offset, PSIZE_T view_size,
                                              SECTION_INHERIT inherit, ULONG allocation, ULONG protect);
NTKERNELAPI NTSTATUS NTAPI ZwUnmapViewOfSection(HANDLE process, PVOID base);
NTKERNELAPI NTSTATUS NTAPI ZwAllocateVirtualMemory(HANDLE process, PVOID *base, ULONG_PTR zero_bits,
                                                   PSIZE_T size, ULONG type, ULONG protect);
NTKERNELAPI NTSTATUS NTAPI ZwFreeVirtualMemory(HANDLE process, PVOID *base, PSIZE_T size, ULONG type);
NTKERNELAPI NTSTATUS NTAPI ZwWaitForSingleObject(HANDLE handle, BOOLEAN alertable, PLARGE_INTEGER timeout);
NTKERNELAPI NTSTATUS NTAPI ZwCreateEvent(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                         EVENT_TYPE type, BOOLEAN state);
NTKERNELAPI NTSTATUS NTAPI ZwSetEvent(HANDLE handle, PLONG previous);
NTKERNELAPI NTSTATUS NTAPI ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length,
                                                    PULONG returned);

/*
 * mime paravirtual hardware access for drivers (the mime HAL). win32k uses
 * these to talk to the host display and input devices.
 */
NTKERNELAPI ULONG64 NTAPI HalMimeHypercall(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4,
                                           ULONG64 a5, ULONG64 a6, PULONG64 value);
NTKERNELAPI PKEVENT NTAPI HalMimeGetInputEvent(void);
/* game controllers, from the host's input events: their hid and xusb devices (io/hid.c). */
struct MimeInputEvent;
NTKERNELAPI void NTAPI MimePadEvent(const struct MimeInputEvent *ev);
/* a controller's hid collection for raw input: its interface path, preparsed data and current report. */
typedef struct MIME_PAD_HID {
    WCHAR Path[128];
    const UCHAR *Preparsed;
    ULONG PreparsedLength;
    USHORT VendorId, ProductId, Version, UsagePage, Usage;
    UCHAR Report[15];
} MIME_PAD_HID;
NTKERNELAPI BOOLEAN NTAPI MimePadHidInfo(ULONG host, MIME_PAD_HID *info);
NTKERNELAPI PKEVENT NTAPI HalMimeGetGpuEvent(void);
/* copies that treat the pointer as user memory and recover from faults, for drivers. */
NTKERNELAPI NTSTATUS NTAPI MimeCopyFromUser(void *dst, const void *user_src, SIZE_T length);
NTKERNELAPI NTSTATUS NTAPI MimeCopyToUser(void *user_dst, const void *src, SIZE_T length);
NTKERNELAPI const void *NTAPI HalMimeGetBootInfo(void);
NTKERNELAPI PVOID NTAPI MmAllocateContiguousPages(SIZE_T bytes);
NTKERNELAPI void NTAPI MmFreeContiguousPages(PVOID va, SIZE_T bytes);
NTKERNELAPI NTSTATUS NTAPI MmCreateSharedSection(SIZE_T size, PVOID *kernel_base, PVOID *section);
NTKERNELAPI NTSTATUS NTAPI MmMapSharedSectionUser(PVOID section, PEPROCESS process, PVOID *user_base,
                                                  BOOLEAN writable);
NTKERNELAPI void NTAPI MmSetProcessCallbackTable(PEPROCESS process, PVOID table);
/* a section over physical pages the caller owns and frees once the section is gone. */
NTKERNELAPI NTSTATUS NTAPI MimeMmCreatePhysicalSection(ULONG64 pa, SIZE_T size, PVOID *section);

#endif
