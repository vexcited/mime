#ifndef MIME_SHELL32_PRIVATE_H
#define MIME_SHELL32_PRIVATE_H

#define SHSTDAPI DECLSPEC_EXPORT
#include <mime/win32/shobjidl.h>
#include <mime/win32/shellapi.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

/* user32 exports that winuser.h does not declare yet. */
WINUSERAPI HICON WINAPI CopyIcon(HICON icon);
typedef BOOL(CALLBACK *PROPENUMPROCEXW)(HWND, LPWSTR, HANDLE, ULONG_PTR);
WINUSERAPI BOOL WINAPI PrintWindow(HWND window, HDC dc, UINT flags);
WINUSERAPI int WINAPI EnumPropsExW(HWND window, PROPENUMPROCEXW proc, LPARAM parameter);

/* the process heap is the task allocator of mime's ole32, so callers free this with CoTaskMemFree or ILFree. */
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

/* the caller frees the copy with CoTaskMemFree. */
LPWSTR ShDuplicate(LPCWSTR text);
/* NULL stays NULL. */
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

/* iconart.c: the pictures shell32 draws itself. */
enum {
    ART_DOC,
    ART_DOCASSOC,
    ART_APP,
    ART_FOLDER,
    ART_FOLDEROPEN,
    ART_DRIVE,
    ART_DRIVE_REMOVABLE,
    ART_DRIVE_OPTICAL,
    ART_DRIVE_NETWORK,
    ART_DRIVE_MEMORY,
    ART_COMPUTER,
    ART_LINK,
};
DWORD *ShIconArt(int kind, int cx, int cy);

/*
 * a stock icon by number, or an index into a file's icons where a negative index is minus a resource id. Fallback is
 * the stock icon used when the file has none.
 */
typedef struct _SH_ICON_SOURCE {
    int Stock;
    int Index;
    int Fallback;
    /* the file shows its own icon, like an executable. */
    BOOL Self;
    WCHAR Path[MAX_PATH];
} SH_ICON_SOURCE;

/* icons.c */
BOOL ShIconFindFile(LPCWSTR file, WCHAR *path);
UINT ShIconCount(LPCWSTR path);
HICON ShIconLoad(LPCWSTR path, int index, int cx, int cy);
HICON ShIconFromPixels(const DWORD *pixels, int cx, int cy);
DWORD *ShIconPixels(HICON icon, int *cx, int *cy);
HICON ShDecorateIcon(HICON base, HICON overlay, BOOL selected);
HICON ShLoadStockIcon(int siid, int cx, int cy);
void ShIconSourceStock(int siid, SH_ICON_SOURCE *source);
void ShIconSourceForPath(LPCWSTR path, DWORD attributes, BOOL disk, BOOL open, SH_ICON_SOURCE *source);
HICON ShLoadIconSource(const SH_ICON_SOURCE *source, int cx, int cy);
void ShIconSourceKey(const SH_ICON_SOURCE *source, WCHAR *key, SIZE_T chars);
BOOL ShIconSourceLocation(const SH_ICON_SOURCE *source, WCHAR *path, int *index);
HICON ShIconForRequest(const SH_ICON_SOURCE *source, UINT flags);
#define SH_STOCK_COMPUTER 0x1000
#define SH_STOCK_DESKTOP 0x1001
#define SH_STOCK_SYSTEMDRIVE 0x1002

/* sysimagelist.c */
struct _IMAGELIST *ShSystemList(int which);
int ShSystemIconIndex(const SH_ICON_SOURCE *source, UINT small);

/* thumbnail.c */
HRESULT ShItemImage(LPCWSTR path, int virtual_kind, SIZE size, int flags, HBITMAP *bitmap);

#endif
