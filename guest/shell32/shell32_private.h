/*
 * shell32 internal interface shared by its source files.
 */
#ifndef MIME_SHELL32_PRIVATE_H
#define MIME_SHELL32_PRIVATE_H

#define SHSTDAPI DECLSPEC_EXPORT
#include <mime/win32/shobjidl.h>
#include <mime/win32/shellapi.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

/* user32 exports that winuser.h does not declare yet. */
WINUSERAPI HICON WINAPI CopyIcon(HICON icon);

/*
 * shell32 allocates what callers free with CoTaskMemFree or ILFree from the
 * process heap, which is the task allocator of mime's ole32.
 */
FORCEINLINE void *ShAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

FORCEINLINE void *ShAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void ShFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* a heap copy of a string, for results the caller frees with CoTaskMemFree. */
LPWSTR ShDuplicate(LPCWSTR text);
/* converts an ansi string to a heap allocated wide string, NULL stays NULL. */
LPWSTR ShWiden(LPCSTR text);

/* pidl.c */
BOOL ShPathFromIdList(PCUIDLIST_ABSOLUTE pidl, LPWSTR path, DWORD chars);
PIDLIST_ABSOLUTE ShIdListFromPath(LPCWSTR path);
PIDLIST_ABSOLUTE ShComputerList(void);
PIDLIST_ABSOLUTE ShClassList(BYTE sort, const GUID *clsid);
BOOL ShIsComputerItem(const BYTE *item);
BOOL ShIdListLastName(PCUIDLIST_ABSOLUTE pidl, LPWSTR name, ULONG chars);
PIDLIST_ABSOLUTE ShChildList(PCIDLIST_ABSOLUTE folder, const WIN32_FIND_DATAW *find);

/* item.c */
SFGAOF ShAttributesFromFile(DWORD attributes);
HRESULT ShParseName(PCWSTR name, IBindCtx *context, PIDLIST_ABSOLUTE *pidl, DWORD *attributes);
HRESULT ShItemFromList(PIDLIST_ABSOLUTE pidl, REFIID riid, void **object);
HRESULT ShArrayFromLists(UINT count, PCIDLIST_ABSOLUTE *lists, IShellItemArray **array);

#endif
