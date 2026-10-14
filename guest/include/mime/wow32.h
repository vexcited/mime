/* fixed-width structures at the 32-bit to native boundary. */
#ifndef MIME_WOW32_H
#define MIME_WOW32_H

#include <mime/nt.h>

typedef struct MIME_EXCEPTION32 {
    NTSTATUS Code;
    ULONG Flags, Record, Address, Count, Information[EXCEPTION_MAXIMUM_PARAMETERS];
} MIME_EXCEPTION32;
MIME_STATIC_ASSERT(sizeof(MIME_EXCEPTION32) == 80, "exception record32 layout");

typedef struct MIME_STRING32 {
    USHORT Length, MaximumLength;
    ULONG Buffer;
} MIME_STRING32;

typedef struct MIME_OBJECT_ATTRIBUTES32 {
    ULONG Length, RootDirectory, ObjectName, Attributes;
    ULONG SecurityDescriptor, SecurityQualityOfService;
} MIME_OBJECT_ATTRIBUTES32;

typedef struct MIME_IO_STATUS32 {
    NTSTATUS Status;
    ULONG Information;
} MIME_IO_STATUS32;

typedef struct MIME_PARAMETERS32 {
    ULONG MaximumLength, Length, Flags, DebugFlags;
    ULONG ConsoleHandle, ConsoleFlags, StandardInput, StandardOutput, StandardError;
    MIME_STRING32 CurrentDirectory;
    ULONG CurrentDirectoryHandle;
    MIME_STRING32 DllPath, ImagePathName, CommandLine;
    ULONG Environment;
    ULONG StartingX, StartingY, CountX, CountY, CountCharsX, CountCharsY;
    ULONG FillAttribute, WindowFlags, ShowWindowFlags;
    MIME_STRING32 WindowTitle, DesktopInfo, ShellInfo, RuntimeData;
    UCHAR CurrentDirectories[0x200];
    ULONG EnvironmentSize, EnvironmentVersion, PackageDependencyData, ProcessGroupId, LoaderThreads;
} MIME_PARAMETERS32;
MIME_FIELD_AT(MIME_PARAMETERS32, ImagePathName, 0x38);
MIME_FIELD_AT(MIME_PARAMETERS32, EnvironmentSize, 0x290);

typedef struct MIME_PEB32 {
    UCHAR InheritedAddressSpace, ReadImageFileExecOptions, BeingDebugged, BitField;
    ULONG Mutant, ImageBaseAddress, Ldr, ProcessParameters, SubSystemData, ProcessHeap, FastPebLock;
    ULONG AtlThunkSListPtr, IFEOKey, CrossProcessFlags, KernelCallbackTable;
    ULONG SystemReserved, AtlThunkSListPtr32, ApiSetMap, TlsExpansionCounter, TlsBitmap, TlsBitmapBits[2];
    ULONG ReadOnlySharedMemoryBase, SharedData, ReadOnlyStaticServerData;
    ULONG AnsiCodePageData, OemCodePageData, UnicodeCaseTableData, NumberOfProcessors, NtGlobalFlag;
    LARGE_INTEGER CriticalSectionTimeout;
    ULONG HeapSegmentReserve, HeapSegmentCommit, HeapDeCommitTotalFreeThreshold, HeapDeCommitFreeBlockThreshold;
    ULONG NumberOfHeaps, MaximumNumberOfHeaps, ProcessHeaps, GdiSharedHandleTable;
    ULONG ProcessStarterHelper, GdiDCAttributeList, LoaderLock, OSMajorVersion, OSMinorVersion;
    USHORT OSBuildNumber, OSCSDVersion;
    ULONG OSPlatformId, ImageSubsystem, ImageSubsystemMajorVersion, ImageSubsystemMinorVersion;
    ULONG ActiveProcessAffinityMask, GdiHandleBuffer[34], PostProcessInitRoutine, TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32], SessionId;
} MIME_PEB32;
MIME_FIELD_AT(MIME_PEB32, SessionId, 0x1D4);

#define MIME_TEB32_SELF 0x18
#define MIME_TEB32_CLIENT_ID 0x20
#define MIME_TEB32_PEB 0x30
#define MIME_TEB32_LOCALE 0xC4
#define MIME_TEB32_STATIC_STRING 0xBF8
#define MIME_TEB32_STATIC_BUFFER 0xC00
#define MIME_TEB32_DEALLOCATION_STACK 0xE0C
#define MIME_TEB32_TLS_LINKS 0xF10
#define MIME_TEB32_REAL_CLIENT_ID 0x6B4

#endif
