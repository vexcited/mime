/*
 * dbghelp: symbols, stack walks and minidumps. there is no pdb reader: a
 * symbol is the nearest export below the address, which is what dbghelp
 * falls back to without symbol files, and there is no line information.
 * stack walks cover the calling process, using unwind data on x64 and the
 * frame chain on x86 and arm64. a minidump holds the system info, the module list
 * and the exception.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>

#define DBGHELP DECLSPEC_EXPORT

typedef unsigned int ULONG32;

#if defined(__x86_64__)
#define NATIVE_MACHINE 0x8664 /* IMAGE_FILE_MACHINE_AMD64 */
#elif defined(__i386__)
#define NATIVE_MACHINE 0x014C /* IMAGE_FILE_MACHINE_I386 */
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

#if defined(__x86_64__) || defined(__aarch64__)
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
#if defined(__x86_64__) || defined(__aarch64__)
    ULONG64 base;
    return own_process(process) ? RtlLookupFunctionEntry(address, &base, nullptr) : nullptr;
#else
    /* x86 code has no function table, only the frame pointer. */
    return nullptr;
#endif
}

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

DBGHELP BOOL WINAPI SymGetLineFromAddr64(HANDLE process, DWORD64 address, PDWORD displacement, PIMAGEHLP_LINE64 line)
{
    SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
}

/* demangles a c++ name down to its qualified name, like UNDNAME_NAME_ONLY. the signature is always left out. */
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

/* moves one frame up, false at the end of the stack. */
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
#elif defined(__i386__)
    /* the frame pointer chain: the caller's pointer sits at the frame and the return address above it. */
    ULONG frame = c->Ebp;
    PNT_TIB tib = (PNT_TIB)NtCurrentTeb();
    if (!frame || (frame & 3) || frame < c->Esp || frame + 8 > (ULONG)(ULONG_PTR)tib->StackBase)
        return FALSE;
    ULONG *words = (ULONG *)(ULONG_PTR)frame;
    c->Eip = words[1];
    c->Esp = frame + 8;
    c->Ebp = words[0];
    return c->Eip != 0;
#else
    /* an unwound frame's pc is the return address, so the call is the instruction before it. */
    ULONG64 base;
    ULONG64 stack = c->Sp, pc = c->Pc;
    PVOID function = RtlLookupFunctionEntry((c->ContextFlags & CONTEXT_UNWOUND_TO_CALL) ? pc - 4 : pc, &base, nullptr);
    if (function) {
        PVOID data;
        ULONG64 establisher;
        RtlVirtualUnwind(0, base, pc, function, c, &data, &establisher, nullptr);
    } else {
        /* a leaf without unwind data returns through lr. */
        c->Pc = c->Lr;
        c->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    }
    return c->Pc != 0 && (c->Sp > stack || (c->Sp == stack && c->Pc != pc));
#endif
}

static void fill_frame(LPSTACKFRAME64 frame, const CONTEXT *c)
{
#ifdef __x86_64__
    frame->AddrPC.Offset = c->Rip;
    frame->AddrStack.Offset = c->Rsp;
    frame->AddrFrame.Offset = c->Rbp;
#elif defined(__i386__)
    frame->AddrPC.Offset = c->Eip;
    frame->AddrStack.Offset = c->Esp;
    frame->AddrFrame.Offset = c->Ebp;
#else
    frame->AddrPC.Offset = c->Pc;
    frame->AddrStack.Offset = c->Sp;
    frame->AddrFrame.Offset = c->Fp;
#endif
    frame->AddrPC.Mode = frame->AddrStack.Mode = frame->AddrFrame.Mode = frame->AddrReturn.Mode = AddrModeFlat;
    CONTEXT next = *c;
    frame->AddrReturn.Offset = 0;
    if (unwind(&next))
#if defined(__x86_64__)
        frame->AddrReturn.Offset = next.Rip;
#elif defined(__i386__)
        frame->AddrReturn.Offset = next.Eip;
#else
        frame->AddrReturn.Offset = next.Pc;
#endif
}

/*
 * like StackWalk64 on Windows with x64 and arm64 contexts, every call after
 * the first moves the context up one frame. Reserved[0] marks the first call.
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

/* MINIDUMP_STRING: byte length, then the UTF-16 text and its terminator. */
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

    /* write the directory and the system info string back in place. */
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

/* ------------------------------------------------------------------ module information */

#define SymExport 4
#define IMAGE_DEBUG_TYPE_CODEVIEW 2
#define CV_SIGNATURE_RSDS 0x53445352

typedef struct DEBUG_DIRECTORY {
    DWORD Characteristics;
    DWORD TimeDateStamp;
    WORD MajorVersion;
    WORD MinorVersion;
    DWORD Type;
    DWORD SizeOfData;
    DWORD AddressOfRawData;
    DWORD PointerToRawData;
} DEBUG_DIRECTORY;

typedef struct CV_INFO_PDB70 {
    DWORD CvSignature;
    GUID Signature;
    DWORD Age;
    char PdbFileName[1];
} CV_INFO_PDB70;

typedef struct IMAGEHLP_MODULEW64 {
    DWORD SizeOfStruct;
    DWORD64 BaseOfImage;
    DWORD ImageSize;
    DWORD TimeDateStamp;
    DWORD CheckSum;
    DWORD NumSyms;
    DWORD SymType;
    WCHAR ModuleName[32];
    WCHAR ImageName[256];
    WCHAR LoadedImageName[256];
    WCHAR LoadedPdbName[256];
    DWORD CVSig;
    WCHAR CVData[MAX_PATH * 3];
    DWORD PdbSig;
    GUID PdbSig70;
    DWORD PdbAge;
    BOOL PdbUnmatched;
    BOOL DbgUnmatched;
    BOOL LineNumbers;
    BOOL GlobalSymbols;
    BOOL TypeInfo;
    BOOL SourceIndexed;
    BOOL Publics;
    DWORD MachineType;
    DWORD Reserved;
} IMAGEHLP_MODULEW64;

typedef struct IMAGEHLP_MODULE64 {
    DWORD SizeOfStruct;
    DWORD64 BaseOfImage;
    DWORD ImageSize;
    DWORD TimeDateStamp;
    DWORD CheckSum;
    DWORD NumSyms;
    DWORD SymType;
    CHAR ModuleName[32];
    CHAR ImageName[256];
    CHAR LoadedImageName[256];
    CHAR LoadedPdbName[256];
    DWORD CVSig;
    CHAR CVData[MAX_PATH * 3];
    DWORD PdbSig;
    GUID PdbSig70;
    DWORD PdbAge;
    BOOL PdbUnmatched;
    BOOL DbgUnmatched;
    BOOL LineNumbers;
    BOOL GlobalSymbols;
    BOOL TypeInfo;
    BOOL SourceIndexed;
    BOOL Publics;
    DWORD MachineType;
    DWORD Reserved;
} IMAGEHLP_MODULE64;

/* finds the codeview record in the debug directory of a mapped or file-layout image. */
static const CV_INFO_PDB70 *codeview(BYTE *base, BOOL mapped, DWORD *size)
{
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!dir->VirtualAddress || !dir->Size || !mapped)
        return nullptr;
    DEBUG_DIRECTORY *entries = (DEBUG_DIRECTORY *)(base + dir->VirtualAddress);
    for (DWORD i = 0; i < dir->Size / sizeof(*entries); i++) {
        if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || !entries[i].AddressOfRawData)
            continue;
        const CV_INFO_PDB70 *cv = (const CV_INFO_PDB70 *)(base + entries[i].AddressOfRawData);
        if (entries[i].SizeOfData < sizeof(*cv) || cv->CvSignature != CV_SIGNATURE_RSDS)
            continue;
        *size = entries[i].SizeOfData;
        return cv;
    }
    return nullptr;
}

/* without symbol files a module reports its exports as SymExport, as Windows does when no pdb is found. */
DBGHELP BOOL WINAPI SymGetModuleInfoW64(HANDLE process, DWORD64 address, IMAGEHLP_MODULEW64 *info)
{
    if (!info || info->SizeOfStruct < offsetof(IMAGEHLP_MODULEW64, LoadedPdbName)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD64 module = SymGetModuleBase64(process, address);
    if (!module) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return FALSE;
    }
    BYTE *base = (BYTE *)(ULONG_PTR)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD size = info->SizeOfStruct > sizeof(*info) ? sizeof(*info) : info->SizeOfStruct;
    memset((BYTE *)info + sizeof(DWORD), 0, size - sizeof(DWORD));
    info->BaseOfImage = module;
    info->ImageSize = nt->OptionalHeader.SizeOfImage;
    info->TimeDateStamp = nt->FileHeader.TimeDateStamp;
    info->CheckSum = nt->OptionalHeader.CheckSum;
    info->SymType = SymExport;
    WCHAR path[MAX_PATH];
    if (GetModuleFileNameW((HMODULE)base, path, MAX_PATH)) {
        lstrcpynW(info->ImageName, path, 256);
        lstrcpynW(info->LoadedImageName, path, 256);
        PCWSTR name = wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path;
        lstrcpynW(info->ModuleName, name, 32);
        PWSTR dot = wcsrchr(info->ModuleName, L'.');
        if (dot)
            *dot = 0;
    }
    IMAGE_DATA_DIRECTORY *exports = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exports->VirtualAddress && exports->Size)
        info->NumSyms = ((IMAGE_EXPORT_DIRECTORY *)(base + exports->VirtualAddress))->NumberOfNames;
    if (size < offsetof(IMAGEHLP_MODULEW64, CVSig))
        return TRUE;
    DWORD cv_size;
    const CV_INFO_PDB70 *cv = codeview(base, TRUE, &cv_size);
    if (cv) {
        info->CVSig = cv->CvSignature;
        MultiByteToWideChar(CP_UTF8, 0, cv->PdbFileName, -1, info->CVData, RTL_NUMBER_OF(info->CVData));
        if (size >= offsetof(IMAGEHLP_MODULEW64, PdbUnmatched)) {
            info->PdbSig70 = cv->Signature;
            info->PdbAge = cv->Age;
        }
    }
    if (size >= offsetof(IMAGEHLP_MODULEW64, Reserved)) {
        info->Publics = info->NumSyms != 0;
        info->MachineType = nt->FileHeader.Machine;
    }
    return TRUE;
}

DBGHELP BOOL WINAPI SymGetModuleInfo64(HANDLE process, DWORD64 address, IMAGEHLP_MODULE64 *info)
{
    if (!info || info->SizeOfStruct < offsetof(IMAGEHLP_MODULE64, LoadedPdbName)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    IMAGEHLP_MODULEW64 wide = {.SizeOfStruct = sizeof(wide)};
    if (!SymGetModuleInfoW64(process, address, &wide))
        return FALSE;
    DWORD size = info->SizeOfStruct > sizeof(*info) ? sizeof(*info) : info->SizeOfStruct;
    memset((BYTE *)info + sizeof(DWORD), 0, size - sizeof(DWORD));
    info->BaseOfImage = wide.BaseOfImage;
    info->ImageSize = wide.ImageSize;
    info->TimeDateStamp = wide.TimeDateStamp;
    info->CheckSum = wide.CheckSum;
    info->NumSyms = wide.NumSyms;
    info->SymType = wide.SymType;
    WideCharToMultiByte(CP_ACP, 0, wide.ModuleName, -1, info->ModuleName, 32, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, wide.ImageName, -1, info->ImageName, 256, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, wide.LoadedImageName, -1, info->LoadedImageName, 256, nullptr, nullptr);
    if (size >= offsetof(IMAGEHLP_MODULE64, PdbSig)) {
        info->CVSig = wide.CVSig;
        WideCharToMultiByte(CP_ACP, 0, wide.CVData, -1, info->CVData, sizeof(info->CVData), nullptr, nullptr);
    }
    if (size >= offsetof(IMAGEHLP_MODULE64, PdbUnmatched)) {
        info->PdbSig70 = wide.PdbSig70;
        info->PdbAge = wide.PdbAge;
    }
    if (size >= offsetof(IMAGEHLP_MODULE64, Reserved)) {
        info->Publics = wide.Publics;
        info->MachineType = wide.MachineType;
    }
    return TRUE;
}

typedef struct SYMSRV_INDEX_INFOW {
    DWORD sizeofstruct;
    WCHAR file[MAX_PATH + 1];
    BOOL stripped;
    DWORD timestamp;
    DWORD size;
    WCHAR dbgfile[MAX_PATH + 1];
    WCHAR pdbfile[MAX_PATH + 1];
    GUID guid;
    DWORD sig;
    DWORD age;
} SYMSRV_INDEX_INFOW;

typedef struct SYMSRV_INDEX_INFO {
    DWORD sizeofstruct;
    CHAR file[MAX_PATH + 1];
    BOOL stripped;
    DWORD timestamp;
    DWORD size;
    CHAR dbgfile[MAX_PATH + 1];
    CHAR pdbfile[MAX_PATH + 1];
    GUID guid;
    DWORD sig;
    DWORD age;
} SYMSRV_INDEX_INFO;

/* the index is the image's timestamp and size, plus the pdb its codeview record names. */
DBGHELP BOOL WINAPI SymSrvGetFileIndexInfoW(PCWSTR file, SYMSRV_INDEX_INFOW *info, DWORD flags)
{
    if (!file || !info || info->sizeofstruct != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HMODULE image = LoadLibraryExW(file, nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!image)
        return FALSE;
    BYTE *base = (BYTE *)((ULONG_PTR)image & ~(ULONG_PTR)3);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    BOOL ok = dos->e_magic == IMAGE_DOS_SIGNATURE;
    if (ok) {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
        memset((BYTE *)info + sizeof(DWORD), 0, sizeof(*info) - sizeof(DWORD));
        PCWSTR name = wcsrchr(file, L'\\') ? wcsrchr(file, L'\\') + 1 : file;
        lstrcpynW(info->file, name, MAX_PATH + 1);
        info->timestamp = nt->FileHeader.TimeDateStamp;
        info->size = nt->OptionalHeader.SizeOfImage;
        info->stripped = (nt->FileHeader.Characteristics & 0x0200) != 0;
        DWORD cv_size;
        const CV_INFO_PDB70 *cv = codeview(base, TRUE, &cv_size);
        if (cv) {
            const char *pdb = strrchr(cv->PdbFileName, '\\') ? strrchr(cv->PdbFileName, '\\') + 1 : cv->PdbFileName;
            MultiByteToWideChar(CP_UTF8, 0, pdb, -1, info->pdbfile, MAX_PATH + 1);
            info->guid = cv->Signature;
            info->age = cv->Age;
        }
    } else {
        SetLastError(ERROR_BAD_EXE_FORMAT);
    }
    FreeLibrary(image);
    return ok;
}

DBGHELP BOOL WINAPI SymSrvGetFileIndexInfo(PCSTR file, SYMSRV_INDEX_INFO *info, DWORD flags)
{
    if (!file || !info || info->sizeofstruct != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, file, -1, path, MAX_PATH);
    SYMSRV_INDEX_INFOW wide = {.sizeofstruct = sizeof(wide)};
    if (!SymSrvGetFileIndexInfoW(path, &wide, flags))
        return FALSE;
    memset((BYTE *)info + sizeof(DWORD), 0, sizeof(*info) - sizeof(DWORD));
    WideCharToMultiByte(CP_ACP, 0, wide.file, -1, info->file, MAX_PATH + 1, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, wide.pdbfile, -1, info->pdbfile, MAX_PATH + 1, nullptr, nullptr);
    info->stripped = wide.stripped;
    info->timestamp = wide.timestamp;
    info->size = wide.size;
    info->guid = wide.guid;
    info->age = wide.age;
    return TRUE;
}

/* ------------------------------------------------------------------ the older forms of the symbol functions */

typedef struct _IMAGEHLP_SYMBOL64 {
    DWORD SizeOfStruct;
    DWORD64 Address;
    DWORD Size;
    DWORD Flags;
    DWORD MaxNameLength;
    CHAR Name[1];
} IMAGEHLP_SYMBOL64, *PIMAGEHLP_SYMBOL64;

typedef BOOL(WINAPI *PSYM_ENUMMODULES_CALLBACK64)(PCSTR name, DWORD64 base, PVOID context);

DBGHELP BOOL WINAPI SymGetSymFromAddr64(HANDLE process, DWORD64 address, PDWORD64 displacement, PIMAGEHLP_SYMBOL64 symbol)
{
    if (!symbol || symbol->SizeOfStruct < sizeof(*symbol) || !symbol->MaxNameLength) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    union {
        SYMBOL_INFO info;
        BYTE storage[sizeof(SYMBOL_INFO) + 512];
    } u = {};
    u.info.SizeOfStruct = sizeof(SYMBOL_INFO);
    u.info.MaxNameLen = 512;
    if (!SymFromAddr(process, address, displacement, &u.info))
        return FALSE;
    symbol->Address = u.info.Address;
    symbol->Size = u.info.Size;
    symbol->Flags = u.info.Flags;
    ULONG n = u.info.NameLen < symbol->MaxNameLength - 1 ? u.info.NameLen : symbol->MaxNameLength - 1;
    memcpy(symbol->Name, u.info.Name, n);
    symbol->Name[n] = 0;
    return TRUE;
}

DBGHELP BOOL WINAPI SymUnDName64(PIMAGEHLP_SYMBOL64 symbol, PSTR out, DWORD size)
{
    if (!symbol || !out || !size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return UnDecorateSymbolName(symbol->Name, out, size, 0) != 0;
}

/* the modules are the loaded ones of this process, named without the extension. */
DBGHELP BOOL WINAPI SymEnumerateModules64(HANDLE process, PSYM_ENUMMODULES_CALLBACK64 callback, PVOID context)
{
    if (!callback || !own_process(process)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LIST_ENTRY *head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
    for (LIST_ENTRY *at = head->Flink; at != head; at = at->Flink) {
        LDR_DATA_TABLE_ENTRY *entry = CONTAINING_RECORD(at, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        char name[MAX_PATH];
        int n = WideCharToMultiByte(CP_ACP, 0, entry->BaseDllName.Buffer, entry->BaseDllName.Length / sizeof(WCHAR), name,
                                    sizeof(name) - 1, nullptr, nullptr);
        name[n] = 0;
        char *dot = strrchr(name, '.');
        if (dot)
            *dot = 0;
        if (!callback(name, (DWORD64)(ULONG_PTR)entry->DllBase, context))
            break;
    }
    return TRUE;
}

/* nothing is read ahead, so loading a module is only learning where it is. */
DBGHELP DWORD64 WINAPI SymLoadModule64(HANDLE process, HANDLE file, PCSTR image, PCSTR module, DWORD64 base, DWORD size)
{
    if (base)
        return base;
    HMODULE found = own_process(process) ? GetModuleHandleA(image ? image : module) : nullptr;
    if (!found)
        SetLastError(ERROR_MOD_NOT_FOUND);
    return (DWORD64)(ULONG_PTR)found;
}

DBGHELP BOOL WINAPI SymUnloadModule64(HANDLE process, DWORD64 base)
{
    AcquireSRWLockShared(&g_lock);
    BOOL known = find_session(process) != nullptr;
    ReleaseSRWLockShared(&g_lock);
    if (!known)
        SetLastError(ERROR_INVALID_HANDLE);
    return known;
}

DBGHELP BOOL WINAPI SymGetSearchPath(HANDLE process, PSTR search, DWORD size)
{
    WCHAR wide[MAX_PATH];
    if (!SymGetSearchPathW(process, wide, MAX_PATH))
        return FALSE;
    if (search && size && !WideCharToMultiByte(CP_ACP, 0, wide, -1, search, (int)size, nullptr, nullptr))
        search[size - 1] = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ the forms with 32 bit addresses */

typedef struct _IMAGEHLP_SYMBOL {
    DWORD SizeOfStruct;
    DWORD Address;
    DWORD Size;
    DWORD Flags;
    DWORD MaxNameLength;
    CHAR Name[1];
} IMAGEHLP_SYMBOL, *PIMAGEHLP_SYMBOL;

typedef struct _IMAGEHLP_MODULE {
    DWORD SizeOfStruct;
    DWORD BaseOfImage;
    DWORD ImageSize;
    DWORD TimeDateStamp;
    DWORD CheckSum;
    DWORD NumSyms;
    DWORD SymType;
    CHAR ModuleName[32];
    CHAR ImageName[256];
    CHAR LoadedImageName[256];
} IMAGEHLP_MODULE, *PIMAGEHLP_MODULE;

DBGHELP BOOL WINAPI SymGetSymFromAddr(HANDLE process, DWORD address, PDWORD displacement, PIMAGEHLP_SYMBOL symbol)
{
    if (!symbol || symbol->SizeOfStruct < sizeof(*symbol) || !symbol->MaxNameLength) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    union {
        IMAGEHLP_SYMBOL64 wide;
        BYTE storage[sizeof(IMAGEHLP_SYMBOL64) + 512];
    } u = {};
    u.wide.SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
    u.wide.MaxNameLength = 512;
    DWORD64 shift = 0;
    if (!SymGetSymFromAddr64(process, address, &shift, &u.wide))
        return FALSE;
    symbol->Address = (DWORD)u.wide.Address;
    symbol->Size = u.wide.Size;
    symbol->Flags = u.wide.Flags;
    DWORD length = (DWORD)strlen(u.wide.Name);
    DWORD n = length < symbol->MaxNameLength - 1 ? length : symbol->MaxNameLength - 1;
    memcpy(symbol->Name, u.wide.Name, n);
    symbol->Name[n] = 0;
    if (displacement)
        *displacement = (DWORD)shift;
    return TRUE;
}

DBGHELP BOOL WINAPI SymGetModuleInfo(HANDLE process, DWORD address, PIMAGEHLP_MODULE info)
{
    if (!info || info->SizeOfStruct < sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    IMAGEHLP_MODULE64 wide = {.SizeOfStruct = sizeof(wide)};
    if (!SymGetModuleInfo64(process, address, &wide))
        return FALSE;
    memset((BYTE *)info + sizeof(DWORD), 0, sizeof(*info) - sizeof(DWORD));
    info->BaseOfImage = (DWORD)wide.BaseOfImage;
    info->ImageSize = wide.ImageSize;
    info->TimeDateStamp = wide.TimeDateStamp;
    info->CheckSum = wide.CheckSum;
    info->NumSyms = wide.NumSyms;
    info->SymType = wide.SymType;
    memcpy(info->ModuleName, wide.ModuleName, sizeof(info->ModuleName));
    memcpy(info->ImageName, wide.ImageName, sizeof(info->ImageName));
    memcpy(info->LoadedImageName, wide.LoadedImageName, sizeof(info->LoadedImageName));
    return TRUE;
}

DBGHELP DWORD WINAPI SymLoadModule(HANDLE process, HANDLE file, PCSTR image, PCSTR module, DWORD base, DWORD size)
{
    return (DWORD)SymLoadModule64(process, file, image, module, base, size);
}

DBGHELP BOOL WINAPI SymUnloadModule(HANDLE process, DWORD base)
{
    return SymUnloadModule64(process, base);
}
