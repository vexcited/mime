#ifndef NDRGEN_APPINFO_H
#define NDRGEN_APPINFO_H

#include <mime/win32/rpcndr.h>

#define NDRGEN_UNUSED __attribute__((unused))

typedef struct APP_MONITOR_POINT APP_MONITOR_POINT;
typedef struct APP_STARTUP_INFO APP_STARTUP_INFO;
typedef struct APP_PROCESS_INFORMATION APP_PROCESS_INFORMATION;

struct APP_MONITOR_POINT {
    long MonitorLeft;
    long MonitorRight;
};
struct APP_STARTUP_INFO {
    unsigned short *lpszTitle;
    long dwX;
    long dwY;
    long dwXSize;
    long dwYSize;
    long dwXCountChars;
    long dwYCountChars;
    long dwFillAttribute;
    long dwFlags;
    short wShowWindow;
    APP_MONITOR_POINT MonitorPoint;
};
struct APP_PROCESS_INFORMATION {
    UINT_PTR ProcessHandle;
    UINT_PTR ThreadHandle;
    long ProcessId;
    long ThreadId;
};

#ifdef _WIN64
_Static_assert(sizeof(APP_MONITOR_POINT) == 8, "size of APP_MONITOR_POINT");
_Static_assert(__builtin_offsetof(APP_MONITOR_POINT, MonitorLeft) == 0, "APP_MONITOR_POINT.MonitorLeft");
_Static_assert(__builtin_offsetof(APP_MONITOR_POINT, MonitorRight) == 4, "APP_MONITOR_POINT.MonitorRight");
_Static_assert(sizeof(APP_STARTUP_INFO) == 56, "size of APP_STARTUP_INFO");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, lpszTitle) == 0, "APP_STARTUP_INFO.lpszTitle");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwX) == 8, "APP_STARTUP_INFO.dwX");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwY) == 12, "APP_STARTUP_INFO.dwY");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwXSize) == 16, "APP_STARTUP_INFO.dwXSize");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwYSize) == 20, "APP_STARTUP_INFO.dwYSize");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwXCountChars) == 24, "APP_STARTUP_INFO.dwXCountChars");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwYCountChars) == 28, "APP_STARTUP_INFO.dwYCountChars");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwFillAttribute) == 32, "APP_STARTUP_INFO.dwFillAttribute");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwFlags) == 36, "APP_STARTUP_INFO.dwFlags");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, wShowWindow) == 40, "APP_STARTUP_INFO.wShowWindow");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, MonitorPoint) == 44, "APP_STARTUP_INFO.MonitorPoint");
_Static_assert(sizeof(APP_PROCESS_INFORMATION) == 24, "size of APP_PROCESS_INFORMATION");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ProcessHandle) == 0, "APP_PROCESS_INFORMATION.ProcessHandle");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ThreadHandle) == 8, "APP_PROCESS_INFORMATION.ThreadHandle");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ProcessId) == 16, "APP_PROCESS_INFORMATION.ProcessId");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ThreadId) == 20, "APP_PROCESS_INFORMATION.ThreadId");
#else
_Static_assert(sizeof(APP_MONITOR_POINT) == 8, "size of APP_MONITOR_POINT");
_Static_assert(__builtin_offsetof(APP_MONITOR_POINT, MonitorLeft) == 0, "APP_MONITOR_POINT.MonitorLeft");
_Static_assert(__builtin_offsetof(APP_MONITOR_POINT, MonitorRight) == 4, "APP_MONITOR_POINT.MonitorRight");
_Static_assert(sizeof(APP_STARTUP_INFO) == 48, "size of APP_STARTUP_INFO");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, lpszTitle) == 0, "APP_STARTUP_INFO.lpszTitle");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwX) == 4, "APP_STARTUP_INFO.dwX");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwY) == 8, "APP_STARTUP_INFO.dwY");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwXSize) == 12, "APP_STARTUP_INFO.dwXSize");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwYSize) == 16, "APP_STARTUP_INFO.dwYSize");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwXCountChars) == 20, "APP_STARTUP_INFO.dwXCountChars");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwYCountChars) == 24, "APP_STARTUP_INFO.dwYCountChars");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwFillAttribute) == 28, "APP_STARTUP_INFO.dwFillAttribute");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, dwFlags) == 32, "APP_STARTUP_INFO.dwFlags");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, wShowWindow) == 36, "APP_STARTUP_INFO.wShowWindow");
_Static_assert(__builtin_offsetof(APP_STARTUP_INFO, MonitorPoint) == 40, "APP_STARTUP_INFO.MonitorPoint");
_Static_assert(sizeof(APP_PROCESS_INFORMATION) == 16, "size of APP_PROCESS_INFORMATION");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ProcessHandle) == 0, "APP_PROCESS_INFORMATION.ProcessHandle");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ThreadHandle) == 4, "APP_PROCESS_INFORMATION.ThreadHandle");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ProcessId) == 8, "APP_PROCESS_INFORMATION.ProcessId");
_Static_assert(__builtin_offsetof(APP_PROCESS_INFORMATION, ThreadId) == 12, "APP_PROCESS_INFORMATION.ThreadId");
#endif

/* ------------------------------------------------------------------ interface appinfo */

#ifndef APPINFO_CLIENT_ONLY
extern long WINAPI appinfo_RAiLaunchAdminProcess_s(handle_t h, unsigned short *ExecutablePath, unsigned short *CommandLine, long StartFlags, long CreateFlags, unsigned short *CurrentDirectory, unsigned short *WindowStation, APP_STARTUP_INFO *StartupInfo, UINT_PTR hWnd, long Timeout, APP_PROCESS_INFORMATION *ProcessInformation, long *ElevationType);
#endif
void *WINAPI MIDL_user_allocate(size_t);
void WINAPI MIDL_user_free(void *);

static const unsigned char appinfo_TypeFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 6: pointer */
    0x11, 0x08, 0x25, 0x5c, /* 10: pointer */
    0x11, 0x08, 0x25, 0x5c, /* 14: pointer */
    0x1a, 0x03, 0x38, 0x00, 0x00, 0x00, 0x12, 0x00, 0x36, 0x08, 0x08, 0x08, /* 18: struct APP_STARTUP_INFO (BOGUS) */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x06, 0x4c, 0x02, 0x08, 0x00, 0x40, 0x5b,
    0x12, 0x08, 0x25, 0x5c,
    0x15, 0x03, 0x08, 0x00, 0x08, 0x08, 0x5b, /* 46: struct APP_MONITOR_POINT (STRUCT) */
    0x1a, 0x03, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb9, 0xb9, 0x08, 0x08, /* 53: struct APP_PROCESS_INFORMATION (BOGUS) */
    0x5b,
#else
    0x00, 0x00, /* 0: pad */
    0x12, 0x08, 0x25, 0x5c, /* 2: pointer */
    0x12, 0x08, 0x25, 0x5c, /* 6: pointer */
    0x11, 0x08, 0x25, 0x5c, /* 10: pointer */
    0x11, 0x08, 0x25, 0x5c, /* 14: pointer */
    0x16, 0x03, 0x30, 0x00, 0x4b, 0x5c, 0x46, 0x5c, 0x00, 0x00, 0x00, 0x00, /* 18: struct APP_STARTUP_INFO (PSTRUCT) */
    0x12, 0x08, 0x25, 0x5c, 0x5b, 0x36, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x06, 0x3e, 0x43, 0x3d, 0x5b,
    0x15, 0x03, 0x10, 0x00, 0xb9, 0xb9, 0x08, 0x08, 0x5b, /* 49: struct APP_PROCESS_INFORMATION (STRUCT) */
#endif
};

static const unsigned char appinfo_ProcFormat[] = {
#ifdef _WIN64
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x68, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 2: appinfo.RAiLaunchAdminProcess */
    0x0f, 0x00, 0x47, 0x0c, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x10, 0x00,
    0x06, 0x00, 0x48, 0x00, 0x18, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x20, 0x00,
    0x08, 0x5c, 0x0b, 0x00, 0x28, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x30, 0x00,
    0x0e, 0x00, 0x0b, 0x01, 0x38, 0x00, 0x12, 0x00, 0x48, 0x00, 0x40, 0x00,
    0xb9, 0x5c, 0x48, 0x00, 0x48, 0x00, 0x08, 0x5c, 0x13, 0x61, 0x50, 0x00,
    0x35, 0x00, 0x50, 0x21, 0x58, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x60, 0x00,
    0x08, 0x5c,
#else
    0x00, 0x00, /* 0: pad */
    0x00, 0x40, 0x00, 0x00, 0x34, 0x00, 0x32, 0x00, 0x00, 0x00, 0x1f, 0x00, /* 2: appinfo.RAiLaunchAdminProcess */
    0x23, 0x00, 0x46, 0x0c, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x48, 0x00, 0x0c, 0x00, 0x08, 0x5c, 0x48, 0x00, 0x10, 0x00, 0x08, 0x5c,
    0x0b, 0x00, 0x14, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x18, 0x00, 0x0e, 0x00,
    0x0b, 0x01, 0x1c, 0x00, 0x12, 0x00, 0x48, 0x00, 0x20, 0x00, 0xb9, 0x5c,
    0x48, 0x00, 0x24, 0x00, 0x08, 0x5c, 0x12, 0x41, 0x28, 0x00, 0x31, 0x00,
    0x50, 0x21, 0x2c, 0x00, 0x08, 0x5c, 0x70, 0x00, 0x30, 0x00, 0x08, 0x5c,
#endif
};

#ifdef _WIN64
#else
#endif

static const unsigned short appinfo_FormatStringOffsetTable[] = {
#ifdef _WIN64
    2
#else
    2
#endif
};

static RPC_CLIENT_INTERFACE appinfo_c_ifspec_data = { sizeof(RPC_CLIENT_INTERFACE),
    {{0x201ef99a, 0x7fa0, 0x444c, {0x93, 0x99, 0x19, 0xba, 0x84, 0xf1, 0x2a, 0x1a}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    0, 0, 0, 0, 0, 0 };
RPC_IF_HANDLE appinfo_c_ifspec = (RPC_IF_HANDLE)&appinfo_c_ifspec_data;

static MIDL_STUB_DESC appinfo_StubDesc = {
    .RpcInterfaceInformation = &appinfo_c_ifspec_data,
    .pfnAllocate = MIDL_user_allocate,
    .pfnFree = MIDL_user_free,
    .pFormatTypes = appinfo_TypeFormat,
    .fCheckBounds = 1,
    .Version = 0x60001,
    .MIDLVersion = 0x8000253,
    .mFlags = 1,
};

#ifndef APPINFO_CLIENT_ONLY
static const SERVER_ROUTINE appinfo_ServerRoutineTable[] = {
    (SERVER_ROUTINE)(void *)appinfo_RAiLaunchAdminProcess_s
};

int (*appinfo_hook)(PRPC_MESSAGE);

static void __RPC_STUB appinfo_dispatch_0(PRPC_MESSAGE m)
{
    if (appinfo_hook && appinfo_hook(m))
        return;
    NdrServerCall2(m);
}
static RPC_DISPATCH_FUNCTION appinfo_DispatchFunctions[] = {
appinfo_dispatch_0
};
static RPC_DISPATCH_TABLE appinfo_DispatchTable = { 1, appinfo_DispatchFunctions, 0 };
static MIDL_SERVER_INFO appinfo_ServerInfo = {
    .pStubDesc = &appinfo_StubDesc,
    .DispatchTable = appinfo_ServerRoutineTable,
    .ProcString = appinfo_ProcFormat,
    .FmtStringOffset = appinfo_FormatStringOffsetTable,
    .nCount = 0,
};
static RPC_SERVER_INTERFACE appinfo_s_ifspec_data = { sizeof(RPC_SERVER_INTERFACE),
    {{0x201ef99a, 0x7fa0, 0x444c, {0x93, 0x99, 0x19, 0xba, 0x84, 0xf1, 0x2a, 0x1a}}, {1, 0}},
    {{0x8A885D04, 0x1CEB, 0x11C9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}},
    &appinfo_DispatchTable, 0, 0, 0, &appinfo_ServerInfo, 0 };
RPC_IF_HANDLE appinfo_s_ifspec = (RPC_IF_HANDLE)&appinfo_s_ifspec_data;
#endif

static NDRGEN_UNUSED long WINAPI appinfo_RAiLaunchAdminProcess(handle_t h, unsigned short *ExecutablePath, unsigned short *CommandLine, long StartFlags, long CreateFlags, unsigned short *CurrentDirectory, unsigned short *WindowStation, APP_STARTUP_INFO *StartupInfo, UINT_PTR hWnd, long Timeout, APP_PROCESS_INFORMATION *ProcessInformation, long *ElevationType)
{
#ifdef _WIN64
    return (long)NdrClientCall2(&appinfo_StubDesc, appinfo_ProcFormat + appinfo_FormatStringOffsetTable[0], h, ExecutablePath, CommandLine, StartFlags, CreateFlags, CurrentDirectory, WindowStation, StartupInfo, hWnd, Timeout, ProcessInformation, ElevationType).Simple;
#else
#pragma pack(push, 4)
    struct {
        handle_t h;
        unsigned short *ExecutablePath;
        unsigned short *CommandLine;
        long StartFlags;
        long CreateFlags;
        unsigned short *CurrentDirectory;
        unsigned short *WindowStation;
        APP_STARTUP_INFO *StartupInfo;
        UINT_PTR hWnd;
        long Timeout;
        APP_PROCESS_INFORMATION *ProcessInformation;
        long *ElevationType;
    } frame = { h, ExecutablePath, CommandLine, StartFlags, CreateFlags, CurrentDirectory, WindowStation, StartupInfo, hWnd, Timeout, ProcessInformation, ElevationType };
#pragma pack(pop)
    return (long)NdrClientCall2(&appinfo_StubDesc, appinfo_ProcFormat + appinfo_FormatStringOffsetTable[0], (unsigned char *)&frame).Simple;
#endif
}

#endif
