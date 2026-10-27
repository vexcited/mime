#ifndef MIME_CBS_NETFX_H
#define MIME_CBS_NETFX_H

#include <mime/win32/winbase.h>

/*
 * installs the .NET Framework 4.8.1 from Microsoft's offline installer, NDP481-x86-x64-AllOS-ENU.exe, from the 7z
 * archive inside it or from the servicing cabinet for the processor of the machine, x64-Windows10.0-KB5011048-x64.cab
 * and its siblings that the archive holds. windows_root is the Windows directory, such as C:\Windows. progress may be
 * null and gets the work done and the work to do, in bytes. returns ERROR_SUCCESS or a Win32 error.
 *
 * the files go below Microsoft.NET and the global assembly cache, the registry values of the components are
 * written, and the system part of the runtime registration is added. nothing of mscoree.dll is touched and a file
 * of the system directories that is there already is left alone. the native images are not generated.
 */
DWORD NetfxInstallFromPackage(PCWSTR package, PCWSTR windows_root, void (*progress)(void *context, ULONG done, ULONG total),
                              void *context);

#endif
