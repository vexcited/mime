/*
 * features on demand. A feature is a runtime that Windows ships or offers, like the .NET Framework or the WebView2
 * runtime. Its files are fetched from the vendor the first time a program needs them, by features.exe.
 *
 * This header holds what the pieces share. The handler interface is how a feature is put on the drive from the
 * file that was downloaded. The request functions are how the rest of the system asks for a feature.
 */
#ifndef MIME_FEATURE_H
#define MIME_FEATURE_H

#include <mime/win32/winbase.h>

/* The names of the catalog, as the command line and the request functions take them. */
#define MIME_FEATURE_WEBVIEW2 L"WebView2"
#define MIME_FEATURE_NETFX4 L"NetFx4"
#define MIME_FEATURE_POWERSHELL L"PowerShell"
#define MIME_FEATURE_VCRUNTIME_X64 L"VCRuntime.x64"

/* The program that serves requests, below the windows folder. */
#define MIME_FEATURE_PROGRAM L"\\System32\\features.exe"

/*
 * Reports how far a handler is. Done and total are in units of the handler's choice, total is 0 when unknown.
 * Step names what is being done and may be null. A handler stops with ERROR_CANCELLED when this returns FALSE.
 */
typedef BOOL(CALLBACK *MIME_FEATURE_PROGRESS)(void *context, ULONGLONG done, ULONGLONG total, PCWSTR step);

/*
 * Puts a feature on the drive. Package is the path of the file that was downloaded, already verified. Root is the
 * root of the drive the files belong on, with a trailing backslash. Returns a Win32 error, 0 for success.
 */
typedef DWORD (*MIME_FEATURE_INSTALL)(PCWSTR package, PCWSTR root, MIME_FEATURE_PROGRESS progress, void *context);

/*
 * The .NET Framework 4.8.1 handler. The package is NDP481-x86-x64-AllOS-ENU.exe, Microsoft's offline installer. It
 * writes the Framework, its assemblies and its runtime libraries under Root. It is provided by guest/lib/cbs, until
 * then features.exe links a weak stub that returns ERROR_NOT_SUPPORTED. features.exe calls it with a null package to
 * learn whether it is there before it downloads anything, so the real one must do nothing and return
 * ERROR_INVALID_PARAMETER for a null package.
 */
DWORD NetfxInstallFromPackage(PCWSTR package, PCWSTR root, MIME_FEATURE_PROGRESS progress, void *context);

/*
 * The Windows PowerShell 5.1 handler. The package is the update of the Windows Management Framework 5.1 for Windows 8.1,
 * Win8.1AndW2K12R2-KB3191564-x64.msu. It holds the files as patches of each other, which guest/lib/msdelta applies, and
 * every file is checked against the hash of the index of the package. It writes the shell, its engine and its modules under
 * Root. Only for x64, ERROR_NOT_SUPPORTED on another processor.
 */
DWORD PowerShellInstallFromPackage(PCWSTR package, PCWSTR root, MIME_FEATURE_PROGRESS progress, void *context);

/*
 * Asks features.exe to enable a feature and waits for it. The user is asked first unless the policy decided. Returns a
 * Win32 error, 0 when the feature is on the drive. The process that asked carries on when it is 0.
 */
DWORD MimeFeatureRequest(PCWSTR name, DWORD flags);

/*
 * Asks for the feature that holds a font family, for a lookup of a font by name that found none. Returns 0 when the
 * font is on the drive, ERROR_NOT_FOUND when no feature has it.
 */
DWORD MimeFeatureRequestFont(PCWSTR family);

/*
 * Called when a program meant to use a file and did not find it: it starts the program, loads the library or opens the
 * file to read it. When the path is in the folders of a feature that is not on the drive, the feature is requested and
 * TRUE says to try again. A feature is requested once in a process, whatever the answer, so a program that fails twice
 * costs one request. Takes a DOS path or an NT path, or the bare name of a program that a feature holds.
 *
 * A look at a name is not a use and never calls this: the attributes of a file, the listing of a folder, the open of a
 * directory and any query of information answer from the drive as it is, as the folders of a feature are there on
 * Windows. The folders are in the skeleton of the drive.
 */
BOOL MimeFeatureFault(PCWSTR path);

/*
 * For the one look that is a use: the runtime of the Framework finds an assembly of the cache by looking at its names.
 * Caller is the return address of the file function that looked. TRUE says that the shell arrived and to look again.
 */
BOOL MimeFeatureFaultFromRuntime(PCWSTR path, PVOID caller);

/* The environment variable features.exe sets for what it runs, so that a missing file in there asks for nothing. */
#define MIME_FEATURE_BUSY_VARIABLE L"MIME_FEATURE_BUSY"

/* Where the state of the features is kept, and where the policy is. */
#define MIME_FEATURE_STATE_KEY L"SOFTWARE\\Mime\\Features"
#define MIME_FEATURE_POLICY_KEY L"SOFTWARE\\Policies\\Mime\\Features"

#endif
