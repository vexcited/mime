/* OptionalFeatures.exe internals shared by its source files. */
#ifndef MIME_FEATUREP_H
#define MIME_FEATUREP_H

#include "tool.h"
#include <rtl.h>
#include "features.h"
#include <mime/feature.h>
#include <mime/win32/winuser.h>

#if defined(__aarch64__)
#define MACHINE_ARCH FEATURE_ARCH_ARM64
#else
#define MACHINE_ARCH FEATURE_ARCH_X64
#endif

#ifndef ERROR_BAD_NET_RESP
#define ERROR_BAD_NET_RESP 58
#endif

#define CACHE_FOLDER L"ProgramData\\Microsoft\\Windows\\WindowsFeatures"

struct UI;

/* What a reader of the progress keeps between two reads. */
typedef struct METER {
    ULONGLONG RateBytes, RateTick;
    ULONGLONG Rate;           /* Bytes a second, smoothed. */
    LONG Permille;
} METER;

/* The progress as it is shown: the bar in thousandths of the whole request, and two lines of text. */
typedef struct PROGRESS_VIEW {
    LONG Permille;
    BOOL Known;               /* False while nothing tells how far, the bar then slides. */
    WCHAR Line[128];
    WCHAR Detail[160];
} PROGRESS_VIEW;

/* What one request is doing. The worker thread writes the progress, the window reads it. */
typedef struct RUN {
    const FEATURE_DEF *Feature;
    BOOL Console;             /* Print the steps. */
    BOOL Dialogs;             /* Ask and show progress in windows. */
    WCHAR Caller[64];
    DWORD CallerPid;          /* The program that asked, the question goes away with it. */
    WCHAR Source[MAX_PATH];   /* A file to use instead of the download. */
    WCHAR Root[8];            /* The drive root, "C:\". */
    WCHAR Cache[MAX_PATH];
    WCHAR Package[MAX_PATH];  /* The file to verify and install. */
    BOOL PackageOwned;        /* Delete it afterwards. */
    CRITICAL_SECTION Lock;
    FEATURE_STAGE Stage;
    ULONGLONG Done, Total;
    WCHAR Step[128];
    WCHAR Detail[200];        /* What a failure adds to its error. */
    volatile LONG Cancel;
    volatile LONG Cancellable;
    METER ConsoleMeter;
    ULONGLONG ConsoleTick;
    LONG PrintedPermille;
    BOOL BarShown;
    BOOL Finished;            /* The work ended well, the bar is full. */
    struct UI *Ui;
    FEATURE_OPS Ops;
    uint32_t EnableFlags;
    BOOL Remove;              /* The work is turning the feature off. */
    WCHAR Font[64];           /* The family a program asked for, it is ended without it. */
    BOOL Told;                /* The user saw that the program is ended. */
} RUN;

/* platform.c */
void RunInit(RUN *run, const FEATURE_DEF *feature);
void RunReport(RUN *run, ULONGLONG done, ULONGLONG total, PCWSTR step);
void RunSnapshot(RUN *run, FEATURE_STAGE *stage, ULONGLONG *done, ULONGLONG *total, WCHAR *step, SIZE_T chars);
void FillOps(FEATURE_OPS *ops, RUN *run);
void WidenName(const char *name, WCHAR *wide, SIZE_T chars);
DWORD FeatureRequestCount(const FEATURE_DEF *feature);
void RecordRequest(const FEATURE_DEF *feature, PCWSTR caller);
BOOL FeatureLoadState(const FEATURE_DEF *feature, FEATURE_STATE *state, DWORD *error);
/* The version of the feature that is on the drive, empty when none. */
void FeatureInstalledVersion(RUN *run, const FEATURE_DEF *feature, WCHAR *version, SIZE_T chars);
BOOL FeatureFilesPresent(RUN *run, const FEATURE_DEF *feature);
void DeleteTree(PCWSTR path);

/* handlers.c */
uint32_t HandlerAvailable(const FEATURE_DEF *feature);
uint32_t HandlerInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package);
uint32_t HandlerRemove(RUN *run, const FEATURE_DEF *feature);
void HandlerComplete(RUN *run, const FEATURE_DEF *feature);
BOOL HandlerPresent(RUN *run, const FEATURE_DEF *feature);
void HandlerVersion(RUN *run, const FEATURE_DEF *feature, WCHAR *version, SIZE_T chars);

/* download.c */
DWORD DownloadPackage(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination);

/* ranges.c: parts of a file at an address. */
typedef struct RANGES RANGES;
RANGES *RangesOpen(RUN *run, const FEATURE_DEF *feature, PCWSTR url);
void RangesClose(RANGES *ranges);
/* Reads length bytes at the offset. */
DWORD RangesRead(RANGES *ranges, ULONGLONG offset, SIZE_T length, void *buffer);
/* The size of the file, once a read has told it. */
ULONGLONG RangesTotal(const RANGES *ranges);
/* About how many bytes the reads will come to, for the progress. */
void RangesExpect(RANGES *ranges, ULONGLONG bytes);

/* zip.c and wim.c: the fonts of a feature read out of an archive at an address into a pack in the cache. */
DWORD AcquireZip(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination);
DWORD AcquireWim(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination);

/* pack.c: the file of fonts that the archives are read into. */
BOOL PackCreate(PCWSTR path, HANDLE *file);
BOOL PackAppend(HANDLE file, const char *name, const void *data, SIZE_T size);
BOOL PackFinish(HANDLE file, DWORD count);
BOOL PackWalk(const BYTE *data, SIZE_T size, BOOL (*entry)(void *context, const char *name, const BYTE *bytes, SIZE_T length),
              void *context);

/* hashes.c */
BOOL HashMatches(const BYTE *data, SIZE_T size, const char *sha256, const char *sha1);
BOOL FileMatches(PCWSTR path, const char *sha256);

/* fonts.c */
uint32_t FontsInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package);
/* The bytes of a package file, which the caller frees with HeapFree. */
DWORD ReadPackage(PCWSTR path, BYTE **data, SIZE_T *size);
BOOL ManagementPresent(RUN *run);
uint32_t ManagementInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package);
uint32_t ManagementRemove(RUN *run);
uint32_t FontsRemove(RUN *run, const FEATURE_DEF *feature);
BOOL FontsPresent(RUN *run, const FEATURE_DEF *feature);

/* vcruntime.c */
uint32_t VcRuntimeInstall(RUN *run, PCWSTR package);
BOOL VcRuntimePresent(RUN *run);
uint32_t VcRuntimeRemove(RUN *run);

/* catalogs.c */
uint32_t CatalogsInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package);
BOOL CatalogsPresent(RUN *run, const FEATURE_DEF *feature);
uint32_t CatalogsRemove(RUN *run, const FEATURE_DEF *feature);

/* trust.c */
DWORD VerifyPackage(RUN *run, const FEATURE_DEF *feature, PCWSTR path);

/* progress.c */
void MeterRead(METER *meter, RUN *run, PROGRESS_VIEW *view);
/* Draws the bar on the console, at most a few times a second unless forced. */
void ConsoleProgress(RUN *run, BOOL force);
void ConsoleProgressEnd(RUN *run);

/* ui.c */
/* Runs the worker. With dialogs the worker is on a thread and the windows are on this one. */
DWORD UiRun(RUN *run, DWORD (*worker)(RUN *run));
uint32_t UiConsent(RUN *run);
void UiStage(RUN *run);

/* text.c: what a person reads about an error. */
void ErrorMessage(DWORD error, WCHAR *text, SIZE_T chars);

#endif
