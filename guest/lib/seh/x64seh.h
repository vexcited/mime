#ifndef MIME_X64SEH_H
#define MIME_X64SEH_H

#define UNW_FLAG_NHANDLER 0
#define UNW_FLAG_EHANDLER 1
#define UNW_FLAG_UHANDLER 2
#define UNW_FLAG_CHAININFO 4

enum {
    UWOP_PUSH_NONVOL = 0,
    UWOP_ALLOC_LARGE = 1,
    UWOP_ALLOC_SMALL = 2,
    UWOP_SET_FPREG = 3,
    UWOP_SAVE_NONVOL = 4,
    UWOP_SAVE_NONVOL_FAR = 5,
    UWOP_EPILOG = 6,
    UWOP_SPARE_CODE = 7,
    UWOP_SAVE_XMM128 = 8,
    UWOP_SAVE_XMM128_FAR = 9,
    UWOP_PUSH_MACHFRAME = 10,
};

typedef struct _RUNTIME_FUNCTION {
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG UnwindData;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

typedef union _UNWIND_CODE {
    struct {
        UCHAR CodeOffset;
        UCHAR UnwindOp : 4;
        UCHAR OpInfo : 4;
    };
    USHORT FrameOffset;
} UNWIND_CODE;

typedef struct _UNWIND_INFO {
    UCHAR Version : 3;
    UCHAR Flags : 5;
    UCHAR SizeOfProlog;
    UCHAR CountOfCodes;
    UCHAR FrameRegister : 4;
    UCHAR FrameOffset : 4;
    UNWIND_CODE UnwindCode[1];
} UNWIND_INFO;

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind,
} EXCEPTION_DISPOSITION;

struct _DISPATCHER_CONTEXT;
typedef EXCEPTION_DISPOSITION(NTAPI *PEXCEPTION_ROUTINE)(PEXCEPTION_RECORD record, PVOID establisher_frame,
                                                         PCONTEXT context, struct _DISPATCHER_CONTEXT *dispatcher);

typedef struct _DISPATCHER_CONTEXT {
    ULONG64 ControlPc;
    ULONG64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 EstablisherFrame;
    ULONG64 TargetIp;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    PVOID HistoryTable;
    ULONG ScopeIndex;
    ULONG Fill0;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS {
    M128A *FloatingContext[16];
    ULONG64 *IntegerContext[16];
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

#define STATUS_UNWIND ((NTSTATUS)0xC0000027L)
#define STATUS_INVALID_DISPOSITION ((NTSTATUS)0xC0000026L)
#define STATUS_NONCONTINUABLE_EXCEPTION ((NTSTATUS)0xC0000025L)
#define STATUS_UNWIND_CONSOLIDATE ((NTSTATUS)0x80000029L)
#ifndef STATUS_LONGJUMP
#define STATUS_LONGJUMP ((NTSTATUS)0x80000026L)
#endif
#define STATUS_INVALID_UNWIND_TARGET ((NTSTATUS)0xC0000029L)
#define EXCEPTION_NESTED_CALL 0x10
#define EXCEPTION_STACK_INVALID 0x8

/* an entry whose unwind data has the low bit set stands for the entry at that offset, which is the one that describes the code. */
#define RUNTIME_FUNCTION_INDIRECT 1

static inline PRUNTIME_FUNCTION search_table(PRUNTIME_FUNCTION table, ULONG count, ULONG rva, ULONG64 base)
{
    ULONG lo = 0, hi = count;
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        if (rva < table[mid].BeginAddress)
            hi = mid;
        else if (rva >= table[mid].EndAddress)
            lo = mid + 1;
        else if (table[mid].UnwindData & RUNTIME_FUNCTION_INDIRECT)
            return (PRUNTIME_FUNCTION)(base + table[mid].UnwindData - RUNTIME_FUNCTION_INDIRECT);
        else
            return &table[mid];
    }
    return nullptr;
}

#endif
