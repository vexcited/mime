/*
 * winspool internal interface: printer handles and string helpers.
 */
#ifndef MIME_WINSPOOL_PRIVATE_H
#define MIME_WINSPOOL_PRIVATE_H

#define MIME_BUILD_WINSPOOL
#include <mime/printing.h>
#include <mime/win32/winspool.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <rtl.h>

/*
 * there is no print spooler service: the printers are those built in (printers.c), kept by every process
 * that asks, and a job is processed by the process that started it (job.c). OpenPrinter gives a handle on
 * the local print server for no name, and one on a printer for the name of one.
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

/* sets the last error and returns FALSE. */
FORCEINLINE BOOL SpoolFail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

/* the handle behind a value, NULL with ERROR_INVALID_HANDLE when it is not one. */
SpoolHandle *SpoolLookup(HANDLE printer);
/* the handle of a printer, not of the server: NULL with ERROR_INVALID_HANDLE for the server. */
SpoolHandle *SpoolLookupPrinter(HANDLE printer);

/* converts an ansi string with the ansi code page; the result comes from the heap. */
PWSTR SpoolToWide(LPCSTR s);
/* writes a utf-16 string as ansi into a buffer of size bytes; returns FALSE if it does not fit. */
BOOL SpoolToAnsi(PCWSTR s, LPSTR buffer, DWORD size);

/* ------------------------------------------------------------------ printers (printers.c) */

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
/* the index of the printer a name stands for, with an optional \\server\ in front; -1 when there is none. */
int SpoolFindPrinter(LPCWSTR name);
int SpoolDefaultPrinter(void);
BOOL SpoolSetDefaultPrinter(int index);
int SpoolPaperCount(void);
const SpoolPaper *SpoolPaperAt(int index);
const SpoolPaper *SpoolFindPaper(WORD id);
const SpoolPaper *SpoolFindPaperByName(LPCWSTR name);
/* the number of resolutions, and the pair of the one asked for, in dots per inch. */
int SpoolResolutionCount(void);
int SpoolResolutionAt(int index);

/* the device mode of a printer for the user: what was last chosen, else the defaults. a copy to SpoolFree. */
DEVMODEW *SpoolUserDevmode(int printer);
DEVMODEW *SpoolDefaultDevmode(int printer);
/* sets the members src asks for and the printer can do into dest, which is a whole device mode of the printer. */
void SpoolMergeDevmode(int printer, DEVMODEW *dest, const DEVMODEW *src);
/* keeps a device mode as the one the user chose, in the registry too. */
void SpoolStoreUserDevmode(int printer, const DEVMODEW *devmode);
DEVMODEA *SpoolDevmodeToAnsi(const DEVMODEW *devmode);
DEVMODEW *SpoolDevmodeFromAnsi(const DEVMODEA *devmode);

/* a dialog for the settings of a printer, the answer is IDOK or IDCANCEL. */
INT_PTR SpoolPreferences(HWND owner, int printer, DEVMODEW *devmode);

/* ------------------------------------------------------------------ jobs (job.c) */

typedef struct SpoolJob SpoolJob;
DWORD SpoolStartDoc(SpoolHandle *handle, const DOC_INFO_1W *info);
void SpoolAbortJob(SpoolHandle *handle);
DWORD SpoolActiveJobs(int printer);
/* the printed file of a job asked for by the user: a path the caller frees with SpoolFree, nullptr when refused. */
PWSTR SpoolAskOutput(LPCWSTR document, int printer);
BOOL SpoolJobsEnum(int printer, DWORD level, DWORD first, DWORD count, LPBYTE buffer, DWORD size, LPDWORD needed,
                   LPDWORD returned, BOOL ansi);
BOOL SpoolJobGet(int printer, DWORD id, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi);
BOOL SpoolJobSet(int printer, DWORD id, DWORD command);

/* ------------------------------------------------------------------ packing results (pack.c) */

/*
 * a result of structures with strings behind them is built twice, once to size it and once into the
 * buffer: the structures from its start, the strings and blobs from its end, each aligned.
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
