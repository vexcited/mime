#ifndef MIME_REG_H
#define MIME_REG_H

#include "../lib/regfile/regsys.h"
#include <mime/win32/winbase.h>
#include <rtl.h>

WINBASEAPI LSTATUS WINAPI RegConnectRegistryW(LPCWSTR machine, HKEY key, HKEY *result);

/* the operations, in the order their names are looked up. */
enum {
    OP_QUERY,
    OP_ADD,
    OP_DELETE,
    OP_COPY,
    OP_SAVE,
    OP_RESTORE,
    OP_LOAD,
    OP_UNLOAD,
    OP_COMPARE,
    OP_EXPORT,
    OP_IMPORT,
    OP_FLAGS,
    OP_COUNT,
};

extern const WCHAR *const g_operation_names[OP_COUNT];

typedef struct ARGS {
    int Count;
    PWSTR *Items; /* Items[0] is the program, Items[1] the operation. */
} ARGS;

/* a key named on the command line. */
typedef struct TARGET {
    int Root;        /* MIME_REG_ROOT_*. */
    HKEY RootKey;    /* the predefined key, or the one connected to a machine. */
    BOOL Remote;     /* a machine other than this one was named. */
    BOOL Connected;  /* RootKey was opened by a connection. */
    PWSTR Machine;   /* "\\name", or NULL. */
    PWSTR Subkey;    /* below the root, "" for the root itself. */
    PWSTR Full;      /* HKEY_NAME\Subkey. */
    DWORD View;      /* KEY_WOW64_32KEY, KEY_WOW64_64KEY or 0. */
} TARGET;

/* ------------------------------------------------------------------ output and errors */

void Out(PCWSTR format, ...);
void Err(PCWSTR format, ...);
void OutText(PCWSTR text);

/* "ERROR: " and the text of an error, to the error stream; always returns 1. */
int ReportError(DWORD error, int operation);
/* the text of the error, without the prefix, to a stream: 1 is the output and 2 the error stream. */
void ReportPlain(DWORD error, int stream);
void ReportSuccess(void);
/* the message of a syntax error of an operation, and 1. */
int SyntaxError(int operation);
int SyntaxErrorGeneric(void);

/* a question with the answers' letters, YNA by default; the number of the letter, from 1. */
int Prompt(PCWSTR format, PCWSTR argument, PCWSTR letters, BOOL force);

/* ------------------------------------------------------------------ helpers */

PWSTR DuplicateString(PCWSTR text);
void *Allocate(SIZE_T bytes);
void Release(void *memory);
BOOL IsSwitch(PCWSTR argument, PCWSTR name);
BOOL IsHelp(PCWSTR argument);
BOOL ParseView(PCWSTR argument, DWORD *view);
void TrimSpaces(PWSTR text);
BOOL ParseKeyArgument(PCWSTR argument, int operation, TARGET *target);
void FreeTarget(TARGET *target);
BOOL ConnectTarget(TARGET *target, DWORD *error);
void ClosePredefined(TARGET *target);
BOOL WildcardMatch(PCWSTR text, PCWSTR pattern, BOOL case_sensitive);
const WCHAR *TypeName(DWORD type);
/* a number in base 2 to 36 after spaces and an optional minus, stopping at the first other character. false when it has no digit. */
BOOL ParseNumber(PCWSTR text, int base, ULONGLONG *value, BOOL *overflow, PCWSTR *end, BOOL allow_minus);
/* the first of the characters in the text, or NULL. */
PWSTR FindAny(PCWSTR text, PCWSTR characters);
int ParseTypeName(PCWSTR text);

/* the usage of an operation, or of the whole program with -1. */
void Usage(int operation);

/* the operations: the exit code of each is the exit code of the program. */
int DoQuery(const ARGS *args);
int DoAdd(const ARGS *args);
int DoDelete(const ARGS *args);
int DoCopy(const ARGS *args);
int DoSave(const ARGS *args);
int DoRestore(const ARGS *args);
int DoLoad(const ARGS *args);
int DoUnload(const ARGS *args);
int DoCompare(const ARGS *args);
int DoExport(const ARGS *args);
int DoImport(const ARGS *args);
int DoFlags(const ARGS *args);

/* shared by the operations that show a value. */
void ShowValueRow(PCWSTR name, DWORD type, const BYTE *data, DWORD size, PCWSTR separator, int indent, BOOL verbose,
                  PCWSTR multi_separator);

#endif
