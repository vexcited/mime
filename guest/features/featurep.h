/* features.exe internals shared by its source files. */
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

#define CACHE_FOLDER L"ProgramData\\Mime\\Features"

struct UI;

/* What one request is doing. The worker thread writes the progress, the window reads it. */
typedef struct RUN {
    const FEATURE_DEF *Feature;
    BOOL Console;             /* Print the steps. */
    BOOL Dialogs;             /* Ask and show progress in windows. */
    WCHAR Caller[64];
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
    LONG PrintedPercent;
    struct UI *Ui;
    FEATURE_OPS Ops;
    uint32_t EnableFlags;
    BOOL Remove;              /* The work is turning the feature off. */
} RUN;

/* platform.c */
void RunInit(RUN *run, const FEATURE_DEF *feature);
void RunReport(RUN *run, ULONGLONG done, ULONGLONG total, PCWSTR step);
void RunSnapshot(RUN *run, FEATURE_STAGE *stage, ULONGLONG *done, ULONGLONG *total, WCHAR *step, SIZE_T chars);
void FillOps(FEATURE_OPS *ops, RUN *run);
void WidenName(const char *name, WCHAR *wide, SIZE_T chars);
BOOL FeatureLoadState(const FEATURE_DEF *feature, FEATURE_STATE *state, DWORD *error);
/* The version of the feature that is on the drive, empty when none. */
void FeatureInstalledVersion(RUN *run, const FEATURE_DEF *feature, WCHAR *version, SIZE_T chars);
BOOL FeatureFilesPresent(RUN *run, const FEATURE_DEF *feature);
void DeleteTree(PCWSTR path);

/* handlers.c */
uint32_t HandlerAvailable(const FEATURE_DEF *feature);
uint32_t HandlerInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package);
uint32_t HandlerRemove(RUN *run, const FEATURE_DEF *feature);
BOOL HandlerPresent(RUN *run, const FEATURE_DEF *feature);
void HandlerVersion(RUN *run, const FEATURE_DEF *feature, WCHAR *version, SIZE_T chars);

/* download.c */
DWORD DownloadPackage(RUN *run, const FEATURE_DEF *feature, PCWSTR url, PCWSTR destination);

/* trust.c */
DWORD VerifyPackage(RUN *run, const FEATURE_DEF *feature, PCWSTR path);

/* ui.c */
/* Runs the worker. With dialogs the worker is on a thread and the windows are on this one. */
DWORD UiRun(RUN *run, DWORD (*worker)(RUN *run));
uint32_t UiConsent(RUN *run);
void UiStage(RUN *run);

/* text.c: what a person reads about an error. */
void ErrorMessage(DWORD error, WCHAR *text, SIZE_T chars);

#endif
