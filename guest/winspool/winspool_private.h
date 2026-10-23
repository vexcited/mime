#ifndef MIME_WINSPOOL_PRIVATE_H
#define MIME_WINSPOOL_PRIVATE_H

#define MIME_BUILD_WINSPOOL
#include <mime/printing.h>
#include <mime/win32/winspool.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <rtl.h>

/*
 * each process keeps the built-in printers and runs its own jobs; there is no spooler service. OpenPrinter returns a
 * server handle for no name, or a printer handle for a printer name.
 */
struct SpoolJob;

typedef struct SpoolHandle {
    struct SpoolHandle *next;
    ACCESS_MASK access;
    /* the printer of the handle, -1 for the print server. */
    int printer;
    /* the data type of the jobs started on it, and the device mode that was asked for. */
    WCHAR datatype[32];
    DEVMODEW *devmode;
    struct SpoolJob *job;
} SpoolHandle;

FORCEINLINE PVOID SpoolAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void SpoolFree(PVOID p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

FORCEINLINE BOOL SpoolFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

/* returns NULL and sets ERROR_INVALID_HANDLE for an invalid handle. */
SpoolHandle *SpoolLookup(HANDLE printer);
/* requires a printer handle; rejects server handles with ERROR_INVALID_HANDLE. */
SpoolHandle *SpoolLookupPrinter(HANDLE printer);

/* converts an ANSI string with the ANSI code page; the result comes from the heap. */
PWSTR SpoolToWide(LPCSTR s);
/* writes a UTF-16 string as ANSI into a buffer of size bytes; returns FALSE if it does not fit. */
BOOL SpoolToAnsi(PCWSTR s, LPSTR buffer, DWORD size);


typedef struct SpoolPrinter {
    LPCWSTR Name, Driver, Port, Comment, Location, Manufacturer, Model;
    DWORD Attributes;
} SpoolPrinter;

/* a paper the printers know, its size in tenths of a millimeter. */
typedef struct SpoolPaper {
    WORD Id;
    LPCWSTR Name;
    int Width, Height;
} SpoolPaper;

int SpoolPrinterCount(void);
const SpoolPrinter *SpoolPrinterAt(int index);
/* resolves a printer name with an optional \\server\ prefix; returns -1 if absent. */
int SpoolFindPrinter(LPCWSTR name);
int SpoolDefaultPrinter(void);
BOOL SpoolSetDefaultPrinter(int index);
int SpoolPaperCount(void);
const SpoolPaper *SpoolPaperAt(int index);
const SpoolPaper *SpoolFindPaper(WORD id);
const SpoolPaper *SpoolFindPaperByName(LPCWSTR name);
/* returns the resolution count and the requested pair in dots per inch. */
int SpoolResolutionCount(void);
int SpoolResolutionAt(int index);

/* returns a copy of the user's saved device mode or the defaults; free with SpoolFree. */
DEVMODEW *SpoolUserDevmode(int printer);
DEVMODEW *SpoolDefaultDevmode(int printer);
/* merges supported fields requested by src into dest's full device mode. */
void SpoolMergeDevmode(int printer, DEVMODEW *dest, const DEVMODEW *src);
/* saves the user's device mode in memory and the registry. */
void SpoolStoreUserDevmode(int printer, const DEVMODEW *devmode);
DEVMODEA *SpoolDevmodeToAnsi(const DEVMODEW *devmode);
DEVMODEW *SpoolDevmodeFromAnsi(const DEVMODEA *devmode);

/* settings dialog returning IDOK or IDCANCEL. */
INT_PTR SpoolPreferences(HWND owner, int printer, DEVMODEW *devmode);


typedef struct SpoolJob SpoolJob;
DWORD SpoolStartDoc(SpoolHandle *handle, const DOC_INFO_1W *info);
void SpoolAbortJob(SpoolHandle *handle);
DWORD SpoolActiveJobs(int printer);
/* returns a user-selected output path to free with SpoolFree, or nullptr if refused. */
PWSTR SpoolAskOutput(LPCWSTR document, int printer);
BOOL SpoolJobsEnum(int printer, DWORD level, DWORD first, DWORD count, LPBYTE buffer, DWORD size, LPDWORD needed,
                   LPDWORD returned, BOOL ansi);
BOOL SpoolJobGet(int printer, DWORD id, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi);
BOOL SpoolJobSet(int printer, DWORD id, DWORD command);


/*
 * builds twice, first to measure. aligned structures grow from the buffer's start; strings and blobs grow from its
 * end.
 */
typedef struct SpoolPack {
    BYTE *base;
    SIZE_T capacity;
    SIZE_T head, tail;
    BOOL ansi;
    BOOL measure;
} SpoolPack;

void SpoolPackBegin(SpoolPack *p, BYTE *buffer, SIZE_T capacity, BOOL measure, BOOL ansi);
void *SpoolPackStruct(SpoolPack *p, SIZE_T size);
LPVOID SpoolPackString(SpoolPack *p, LPCWSTR text);
LPVOID SpoolPackBlob(SpoolPack *p, const void *data, SIZE_T size);
SIZE_T SpoolPackNeeded(const SpoolPack *p);

/* the environment this build runs as, "Windows x64" or "Windows ARM64". */
PCWSTR SpoolNativeEnvironment(void);
/* maps an environment name to its spool subdirectory, NULL when unknown. */
PCWSTR SpoolEnvironmentDirectory(LPCWSTR environment);

#endif
