/*
 * dbghelp: symbols, stack walks and minidumps, for crash reporters and
 * diagnostics. there is no pdb reader: a symbol is the nearest export of
 * the module below the address, which is what windows' dbghelp falls back
 * to without symbol files, and no line information exists. stack walks
 * cover the calling process, unwinding with the system's unwind data on
 * x64 and the frame chain on arm64. a minidump holds the system
 * information, the module list and the exception.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define DBGHELP DECLSPEC_EXPORT

typedef unsigned int ULONG32;

#ifdef __x86_64__
#define NATIVE_MACHINE 0x8664 /* IMAGE_FILE_MACHINE_AMD64 */
#else
#define NATIVE_MACHINE 0xAA64 /* IMAGE_FILE_MACHINE_ARM64 */
#endif

#define SYMOPT_UNDNAME 0x00000002
#define SYMOPT_DEFERRED_LOADS 0x00000004
#define SYMOPT_LOAD_LINES 0x00000010

#define UNDNAME_NAME_ONLY 0x1000

typedef enum {
    AddrMode1616,
    AddrMode1632,
    AddrModeReal,
    AddrModeFlat,
} ADDRESS_MODE;

typedef struct _ADDRESS64 {
    DWORD64 Offset;
    WORD Segment;
    ADDRESS_MODE Mode;
} ADDRESS64, *LPADDRESS64;

typedef struct _KDHELP64 {
    DWORD64 Thread;
    DWORD ThCallbackStack;
    DWORD ThCallbackBStore;
    DWORD NextCallback;
    DWORD FramePointer;
    DWORD64 KiCallUserMode;
    DWORD64 KeUserCallbackDispatcher;
    DWORD64 SystemRangeStart;
    DWORD64 KiUserExceptionDispatcher;
    DWORD64 StackBase;
    DWORD64 StackLimit;
    DWORD BuildVersion;
    DWORD RetpolineStubFunctionTableSize;
    DWORD64 RetpolineStubFunctionTable;
    DWORD RetpolineStubOffset;
    DWORD RetpolineStubSize;
    DWORD64 Reserved0[2];
} KDHELP64;

typedef struct _STACKFRAME64 {
    ADDRESS64 AddrPC;
    ADDRESS64 AddrReturn;
    ADDRESS64 AddrFrame;
    ADDRESS64 AddrStack;
    ADDRESS64 AddrBStore;
    PVOID FuncTableEntry;
    DWORD64 Params[4];
    BOOL Far;
    BOOL Virtual;
    DWORD64 Reserved[3];
    KDHELP64 KdHelp;
} STACKFRAME64, *LPSTACKFRAME64;

typedef struct _SYMBOL_INFO {
    ULONG SizeOfStruct;
    ULONG TypeIndex;
    ULONG64 Reserved[2];
    ULONG Index;
    ULONG Size;
    ULONG64 ModBase;
    ULONG Flags;
    ULONG64 Value;
    ULONG64 Address;
    ULONG Register;
    ULONG Scope;
    ULONG Tag;
    ULONG NameLen;
    ULONG MaxNameLen;
    CHAR Name[1];
} SYMBOL_INFO, *PSYMBOL_INFO;

#define SYMFLAG_EXPORT 0x00000200
#define SymTagFunction 5
#define SymTagPublicSymbol 10

typedef struct _IMAGEHLP_LINE64 IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;

typedef BOOL(WINAPI *PREAD_PROCESS_MEMORY_ROUTINE64)(HANDLE, DWORD64, PVOID, DWORD, LPDWORD);
typedef PVOID(WINAPI *PFUNCTION_TABLE_ACCESS_ROUTINE64)(HANDLE, DWORD64);
typedef DWORD64(WINAPI *PGET_MODULE_BASE_ROUTINE64)(HANDLE, DWORD64);
typedef DWORD64(WINAPI *PTRANSLATE_ADDRESS_ROUTINE64)(HANDLE, HANDLE, LPADDRESS64);

#ifdef __x86_64__
typedef struct _RUNTIME_FUNCTION RUNTIME_FUNCTION_, *PRUNTIME_FUNCTION_;
NTSYSAPI PVOID NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PVOID function, PCONTEXT context,
                                      PVOID *handler_data, PULONG64 establisher, PVOID pointers);
#endif

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ sessions and options */

typedef struct SESSION {
    struct SESSION *Next;
    HANDLE Process;
    WCHAR SearchPath[MAX_PATH];
} SESSION;

static SRWLOCK g_lock = SRWLOCK_INIT;
static SESSION *g_sessions;
static DWORD g_options = SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES;

static SESSION *find_session(HANDLE process)
{
    for (SESSION *s = g_sessions; s; s = s->Next)
        if (s->Process == process)
            return s;
    return nullptr;
}

DBGHELP BOOL WINAPI SymInitializeW(HANDLE process, PCWSTR search, BOOL invade)
{
    AcquireSRWLockExclusive(&g_lock);
    SESSION *s = find_session(process);
    if (!s && (s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s)))) {
        s->Process = process;
        s->Next = g_sessions;
        g_sessions = s;
    }
    if (s && search)
        lstrcpynW(s->SearchPath, search, MAX_PATH);
    ReleaseSRWLockExclusive(&g_lock);
    if (!s)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return s != nullptr;
}

DBGHELP BOOL WINAPI SymInitialize(HANDLE process, PCSTR search, BOOL invade)
{
    WCHAR wide[MAX_PATH];
    if (search)
        MultiByteToWideChar(CP_ACP, 0, search, -1, wide, MAX_PATH);
    return SymInitializeW(process, search ? wide : nullptr, invade);
}

DBGHELP BOOL WINAPI SymCleanup(HANDLE process)
{
    AcquireSRWLockExclusive(&g_lock);
    SESSION *found = nullptr;
    for (SESSION **p = &g_sessions; *p; p = &(*p)->Next) {
        if ((*p)->Process == process) {
            found = *p;
            *p = found->Next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!found) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, found);
    return TRUE;
}

DBGHELP DWORD WINAPI SymGetOptions(void)
{
    return g_options;
}

DBGHELP DWORD WINAPI SymSetOptions(DWORD options)
{
    g_options = options;
    return g_options;
}

DBGHELP BOOL WINAPI SymSetSearchPathW(HANDLE process, PCWSTR search)
{
    AcquireSRWLockExclusive(&g_lock);
    SESSION *s = find_session(process);
    if (s)
        lstrcpynW(s->SearchPath, search ? search : L"", MAX_PATH);
    ReleaseSRWLockExclusive(&g_lock);
    if (!s)
        SetLastError(ERROR_INVALID_HANDLE);
    return s != nullptr;
}

DBGHELP BOOL WINAPI SymGetSearchPathW(HANDLE process, PWSTR search, DWORD size)
{
    AcquireSRWLockShared(&g_lock);
    SESSION *s = find_session(process);
    if (s && search && size)
        lstrcpynW(search, s->SearchPath, (int)size);
    ReleaseSRWLockShared(&g_lock);
    if (!s)
        SetLastError(ERROR_INVALID_HANDLE);
    return s != nullptr;
}

/* ------------------------------------------------------------------ modules and symbols */

static BOOL own_process(HANDLE process)
{
    return process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId();
}

DBGHELP DWORD64 WINAPI SymGetModuleBase64(HANDLE process, DWORD64 address)
{
    HMODULE module = nullptr;
    if (!own_process(process) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(ULONG_PTR)address, &module))
        return 0;
    return (DWORD64)(ULONG_PTR)module;
}

DBGHELP PVOID WINAPI SymFunctionTableAccess64(HANDLE process, DWORD64 address)
{
    ULONG64 base;
    return own_process(process) ? RtlLookupFunctionEntry(address, &base, nullptr) : nullptr;
}

/* the export of a module closest below an address, from its export directory. */
static BOOL nearest_export(BYTE *base, DWORD64 address, const char **name, DWORD64 *start)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir->VirtualAddress || !dir->Size)
        return FALSE;
    IMAGE_EXPORT_DIRECTORY *ex = (IMAGE_EXPORT_DIRECTORY *)(base + dir->VirtualAddress);
    DWORD *functions = (DWORD *)(base + ex->AddressOfFunctions);
    DWORD *names = (DWORD *)(base + ex->AddressOfNames);
    WORD *ordinals = (WORD *)(base + ex->AddressOfNameOrdinals);
    DWORD rva = (DWORD)(address - (DWORD64)(ULONG_PTR)base), best = 0;
    *name = nullptr;
    for (DWORD i = 0; i < ex->NumberOfNames; i++) {
        DWORD f = functions[ordinals[i]];
        /* forwarders point into the export directory and name no code. */
        if (f >= dir->VirtualAddress && f < dir->VirtualAddress + dir->Size)
            continue;
        if (f <= rva && f >= best) {
            best = f;
            *name = (const char *)(base + names[i]);
        }
    }
    *start = (DWORD64)(ULONG_PTR)base + best;
    return *name != nullptr;
}

DBGHELP BOOL WINAPI SymFromAddr(HANDLE process, DWORD64 address, PDWORD64 displacement, PSYMBOL_INFO symbol)
{
    DWORD64 base = SymGetModuleBase64(process, address);
    if (!base) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return FALSE;
    }
    const char *name;
    DWORD64 start;
    if (!symbol || !nearest_export((BYTE *)(ULONG_PTR)base, address, &name, &start)) {
        SetLastError(symbol ? ERROR_INVALID_ADDRESS : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG max = symbol->MaxNameLen;
    ULONG length = (ULONG)strlen(name);
    symbol->ModBase = base;
    symbol->Address = start;
    symbol->Value = 0;
    symbol->Flags = SYMFLAG_EXPORT;
    symbol->Tag = SymTagPublicSymbol;
    symbol->Size = 0;
    symbol->NameLen = length;
    if (max) {
        ULONG n = length < max - 1 ? length : max - 1;
        memcpy(symbol->Name, name, n);
        symbol->Name[n] = 0;
    }
    if (displacement)
        *displacement = address - start;
    return TRUE;
}

/* without symbol files there is no line information. */
DBGHELP BOOL WINAPI SymGetLineFromAddr64(HANDLE process, DWORD64 address, PDWORD displacement, PIMAGEHLP_LINE64 line)
{
    SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
}

/*
 * a decorated c++ name made readable as far as its qualified name: the
 * name and its scopes, as UNDNAME_NAME_ONLY gives them. the signature is
 * left out whatever the flags ask for.
 */
DBGHELP DWORD WINAPI UnDecorateSymbolName(PCSTR decorated, PSTR out, DWORD size, DWORD flags)
{
    if (!decorated || !out || !size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (decorated[0] != '?' || decorated[1] == '?' || decorated[1] == '$') {
        lstrcpynA(out, decorated, (int)size);
        return (DWORD)strlen(out);
    }
    const char *parts[32];
    int lengths[32], n = 0;
    const char *p = decorated + 1;
    while (*p && *p != '@' && n < 32) {
        const char *at = strchr(p, '@');
        if (!at)
            break;
        parts[n] = p;
        lengths[n++] = (int)(at - p);
        p = at + 1;
    }
    DWORD used = 0;
    for (int i = n - 1; i >= 0; i--) {
        for (int k = 0; k < lengths[i] && used + 1 < size; k++)
            out[used++] = parts[i][k];
        if (i && used + 2 < size) {
            out[used++] = ':';
            out[used++] = ':';
        }
    }
    out[used] = 0;
    if (!n)
        lstrcpynA(out, decorated, (int)size);
    return (DWORD)strlen(out);
}

/* ------------------------------------------------------------------ stack walks */

/* one frame up the calling process's stack: false at its end. */
static BOOL unwind(CONTEXT *c)
{
#ifdef __x86_64__
    ULONG64 base;
    PVOID function = RtlLookupFunctionEntry(c->Rip, &base, nullptr);
    if (function) {
        PVOID data;
        ULONG64 establisher;
        RtlVirtualUnwind(0, base, c->Rip, function, c, &data, &establisher, nullptr);
    } else {
        /* a leaf without unwind data: the return address is on top of the stack. */
        c->Rip = *(ULONG64 *)c->Rsp;
        c->Rsp += 8;
    }
    return c->Rip != 0;
#else
    /* windows arm64 code keeps the frame chain: fp points at the saved fp and lr. */
    ULONG64 fp = c->Fp;
    if (!fp || (fp & 7))
        return FALSE;
    c->Pc = ((ULONG64 *)fp)[1];
    c->Fp = ((ULONG64 *)fp)[0];
    c->Sp = fp + 16;
    return c->Pc != 0 && c->Fp > fp;
#endif
}

static void fill_frame(LPSTACKFRAME64 frame, const CONTEXT *c)
{
#ifdef __x86_64__
    frame->AddrPC.Offset = c->Rip;
    frame->AddrStack.Offset = c->Rsp;
    frame->AddrFrame.Offset = c->Rbp;
#else
    frame->AddrPC.Offset = c->Pc;
    frame->AddrStack.Offset = c->Sp;
    frame->AddrFrame.Offset = c->Fp;
#endif
    frame->AddrPC.Mode = frame->AddrStack.Mode = frame->AddrFrame.Mode = frame->AddrReturn.Mode = AddrModeFlat;
    CONTEXT next = *c;
    frame->AddrReturn.Offset = 0;
    if (unwind(&next))
#ifdef __x86_64__
        frame->AddrReturn.Offset = next.Rip;
#else
        frame->AddrReturn.Offset = next.Pc;
#endif
}

/*
 * the context moves one frame up at each call after the first, as windows'
 * StackWalk64 does with x64 and arm64 contexts; Reserved[0] tells the first
 * call from the others.
 */
DBGHELP BOOL WINAPI StackWalk64(DWORD machine, HANDLE process, HANDLE thread, LPSTACKFRAME64 frame, PVOID context,
                                PREAD_PROCESS_MEMORY_ROUTINE64 read, PFUNCTION_TABLE_ACCESS_ROUTINE64 table,
                                PGET_MODULE_BASE_ROUTINE64 module, PTRANSLATE_ADDRESS_ROUTINE64 translate)
{
    if (!frame || !context || machine != NATIVE_MACHINE || !own_process(process)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    CONTEXT *c = context;
    if (frame->Reserved[0] != 1) {
        frame->Reserved[0] = 1;
        fill_frame(frame, c);
        return frame->AddrPC.Offset != 0;
    }
    if (!unwind(c))
        return FALSE;
    fill_frame(frame, c);
    frame->FuncTableEntry = SymFunctionTableAccess64(process, frame->AddrPC.Offset);
    return TRUE;
}

/* ------------------------------------------------------------------ minidumps */

#define MINIDUMP_SIGNATURE 0x504D444D /* "MDMP" */
#define MINIDUMP_VERSION 0xA793
#define ModuleListStream 4
#define ExceptionStream 6
#define SystemInfoStream 7

#pragma pack(push, 4)
typedef struct _MINIDUMP_HEADER {
    ULONG32 Signature;
    ULONG32 Version;
    ULONG32 NumberOfStreams;
    ULONG32 StreamDirectoryRva;
    ULONG32 CheckSum;
    ULONG32 TimeDateStamp;
    ULONG64 Flags;
} MINIDUMP_HEADER;

typedef struct _MINIDUMP_LOCATION_DESCRIPTOR {
    ULONG32 DataSize;
    ULONG32 Rva;
} MINIDUMP_LOCATION_DESCRIPTOR;

typedef struct _MINIDUMP_DIRECTORY {
    ULONG32 StreamType;
    MINIDUMP_LOCATION_DESCRIPTOR Location;
} MINIDUMP_DIRECTORY;

typedef struct _MINIDUMP_SYSTEM_INFO {
    USHORT ProcessorArchitecture;
    USHORT ProcessorLevel;
    USHORT ProcessorRevision;
    UCHAR NumberOfProcessors;
    UCHAR ProductType;
    ULONG32 MajorVersion;
    ULONG32 MinorVersion;
    ULONG32 BuildNumber;
    ULONG32 PlatformId;
    ULONG32 CSDVersionRva;
    USHORT SuiteMask;
    USHORT Reserved2;
    ULONG64 ProcessorFeatures[2];
} MINIDUMP_SYSTEM_INFO;

typedef struct _VS_FIXEDFILEINFO_ {
    DWORD Fields[13];
} VS_FIXEDFILEINFO_;

typedef struct _MINIDUMP_MODULE {
    ULONG64 BaseOfImage;
    ULONG32 SizeOfImage;
    ULONG32 CheckSum;
    ULONG32 TimeDateStamp;
    ULONG32 ModuleNameRva;
    VS_FIXEDFILEINFO_ VersionInfo;
    MINIDUMP_LOCATION_DESCRIPTOR CvRecord;
    MINIDUMP_LOCATION_DESCRIPTOR MiscRecord;
    ULONG64 Reserved0;
    ULONG64 Reserved1;
} MINIDUMP_MODULE;

typedef struct _MINIDUMP_EXCEPTION {
    ULONG32 ExceptionCode;
    ULONG32 ExceptionFlags;
    ULONG64 ExceptionRecord;
    ULONG64 ExceptionAddress;
    ULONG32 NumberParameters;
    ULONG32 __unusedAlignment;
    ULONG64 ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} MINIDUMP_EXCEPTION;

typedef struct _MINIDUMP_EXCEPTION_STREAM {
    ULONG32 ThreadId;
    ULONG32 __alignment;
    MINIDUMP_EXCEPTION ExceptionRecord;
    MINIDUMP_LOCATION_DESCRIPTOR ThreadContext;
} MINIDUMP_EXCEPTION_STREAM;
#pragma pack(pop)

typedef struct _MINIDUMP_EXCEPTION_INFORMATION {
    DWORD ThreadId;
    PEXCEPTION_POINTERS ExceptionPointers;
    BOOL ClientPointers;
} MINIDUMP_EXCEPTION_INFORMATION, *PMINIDUMP_EXCEPTION_INFORMATION;

typedef struct DUMP {
    HANDLE File;
    ULONG32 Rva;
    BOOL Ok;
} DUMP;

static ULONG32 put(DUMP *d, const void *data, ULONG32 size)
{
    ULONG32 at = d->Rva;
    DWORD written;
    if (d->Ok && size && (!WriteFile(d->File, data, size, &written, nullptr) || written != size))
        d->Ok = FALSE;
    d->Rva += size;
    return at;
}

/* a MINIDUMP_STRING: the byte length, then the utf-16 text and its terminator. */
static ULONG32 put_string(DUMP *d, PCWSTR text)
{
    ULONG32 bytes = (ULONG32)wcslen(text) * sizeof(WCHAR);
    ULONG32 at = put(d, &bytes, sizeof(bytes));
    put(d, text, bytes + sizeof(WCHAR));
    return at;
}

DBGHELP BOOL WINAPI MiniDumpWriteDump(HANDLE process, DWORD process_id, HANDLE file, DWORD type,
                                      PMINIDUMP_EXCEPTION_INFORMATION exception, PVOID user_streams, PVOID callback)
{
    if (!own_process(process) || process_id != GetCurrentProcessId()) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (SetFilePointer(file, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DUMP d = {file, 0, TRUE};
    ULONG32 streams = exception ? 3 : 2;
    MINIDUMP_HEADER header = {MINIDUMP_SIGNATURE, MINIDUMP_VERSION, streams, sizeof(MINIDUMP_HEADER), 0, 0, type};
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    header.TimeDateStamp = (ULONG32)(now.QuadPart / 10000000 - 11644473600ll);
    put(&d, &header, sizeof(header));
    MINIDUMP_DIRECTORY directory[3] = {};
    ULONG32 directory_rva = put(&d, directory, streams * sizeof(MINIDUMP_DIRECTORY));

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    RTL_OSVERSIONINFOEXW version = {sizeof(version)};
    RtlGetVersion(&version);
    MINIDUMP_SYSTEM_INFO sys = {};
    sys.ProcessorArchitecture = si.wProcessorArchitecture;
    sys.ProcessorLevel = si.wProcessorLevel;
    sys.ProcessorRevision = si.wProcessorRevision;
    sys.NumberOfProcessors = (UCHAR)si.dwNumberOfProcessors;
    sys.ProductType = 1; /* VER_NT_WORKSTATION */
    sys.MajorVersion = version.dwMajorVersion;
    sys.MinorVersion = version.dwMinorVersion;
    sys.BuildNumber = version.dwBuildNumber;
    sys.PlatformId = 2; /* VER_PLATFORM_WIN32_NT */
    ULONG32 sys_rva = put(&d, &sys, sizeof(sys));
    sys.CSDVersionRva = put_string(&d, L"");
    directory[0] = (MINIDUMP_DIRECTORY){SystemInfoStream, {sizeof(sys), sys_rva}};

    HMODULE modules[512];
    DWORD needed = 0;
    if (!K32EnumProcessModules(process, modules, sizeof(modules), &needed))
        needed = 0;
    ULONG32 count = needed / sizeof(HMODULE);
    if (count > RTL_NUMBER_OF(modules))
        count = RTL_NUMBER_OF(modules);
    MINIDUMP_MODULE *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count ? count : 1) * sizeof(*list));
    if (!list) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (ULONG32 i = 0; i < count; i++) {
        BYTE *base = (BYTE *)modules[i];
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
        WCHAR path[MAX_PATH];
        if (!GetModuleFileNameW(modules[i], path, MAX_PATH))
            path[0] = 0;
        list[i].BaseOfImage = (ULONG64)(ULONG_PTR)base;
        list[i].SizeOfImage = nt->OptionalHeader.SizeOfImage;
        list[i].CheckSum = nt->OptionalHeader.CheckSum;
        list[i].TimeDateStamp = nt->FileHeader.TimeDateStamp;
        list[i].ModuleNameRva = put_string(&d, path);
    }
    ULONG32 modules_rva = put(&d, &count, sizeof(count));
    put(&d, list, count * sizeof(*list));
    HeapFree(GetProcessHeap(), 0, list);
    directory[1] = (MINIDUMP_DIRECTORY){ModuleListStream, {sizeof(count) + count * sizeof(MINIDUMP_MODULE), modules_rva}};

    if (exception) {
        EXCEPTION_POINTERS *pointers = exception->ExceptionPointers;
        MINIDUMP_EXCEPTION_STREAM e = {};
        e.ThreadId = exception->ThreadId;
        if (pointers && pointers->ExceptionRecord) {
            EXCEPTION_RECORD *r = pointers->ExceptionRecord;
            e.ExceptionRecord.ExceptionCode = r->ExceptionCode;
            e.ExceptionRecord.ExceptionFlags = r->ExceptionFlags;
            e.ExceptionRecord.ExceptionAddress = (ULONG64)(ULONG_PTR)r->ExceptionAddress;
            e.ExceptionRecord.NumberParameters = r->NumberParameters;
            for (DWORD i = 0; i < r->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
                e.ExceptionRecord.ExceptionInformation[i] = r->ExceptionInformation[i];
        }
        if (pointers && pointers->ContextRecord) {
            e.ThreadContext.DataSize = sizeof(CONTEXT);
            e.ThreadContext.Rva = put(&d, pointers->ContextRecord, sizeof(CONTEXT));
        }
        ULONG32 at = put(&d, &e, sizeof(e));
        directory[2] = (MINIDUMP_DIRECTORY){ExceptionStream, {sizeof(e), at}};
    }

    /* the directory and the system information's string go back in place. */
    LONG end = (LONG)d.Rva;
    SetFilePointer(file, (LONG)directory_rva, nullptr, FILE_BEGIN);
    d.Rva = directory_rva;
    put(&d, directory, streams * sizeof(MINIDUMP_DIRECTORY));
    SetFilePointer(file, (LONG)sys_rva, nullptr, FILE_BEGIN);
    d.Rva = sys_rva;
    put(&d, &sys, sizeof(sys));
    SetFilePointer(file, end, nullptr, FILE_BEGIN);
    if (!d.Ok)
        return FALSE;
    return TRUE;
}
