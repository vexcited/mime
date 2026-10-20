/*
 * shell api: launching, command lines, dropped files, file information,
 * file operations, the notification area and app bars of shell32.dll.
 */
#ifndef MIME_WIN32_SHELLAPI_H
#define MIME_WIN32_SHELLAPI_H

#include <mime/win32/objbase.h>
#include <mime/win32/winuser.h>

#ifndef SHSTDAPI
#define SHSTDAPI DECLSPEC_IMPORT
#endif

/* the structures of this header are packed on 32 bit windows. */
#ifdef __i386__
#pragma pack(push, 1)
#endif

DECLARE_HANDLE(HDROP);

/* ------------------------------------------------------------------ ShellExecute */

#define SE_ERR_FNF 2
#define SE_ERR_PNF 3
#define SE_ERR_ACCESSDENIED 5
#define SE_ERR_OOM 8
#define SE_ERR_DLLNOTFOUND 32
#define SE_ERR_SHARE 26
#define SE_ERR_ASSOCINCOMPLETE 27
#define SE_ERR_DDETIMEOUT 28
#define SE_ERR_DDEFAIL 29
#define SE_ERR_DDEBUSY 30
#define SE_ERR_NOASSOC 31

#define SEE_MASK_DEFAULT 0x00000000
#define SEE_MASK_CLASSNAME 0x00000001
#define SEE_MASK_CLASSKEY 0x00000003
#define SEE_MASK_IDLIST 0x00000004
#define SEE_MASK_INVOKEIDLIST 0x0000000C
#define SEE_MASK_NOCLOSEPROCESS 0x00000040
#define SEE_MASK_FLAG_NO_UI 0x00000400
#define SEE_MASK_UNICODE 0x00004000
#define SEE_MASK_NOASYNC 0x00000100

typedef struct _SHELLEXECUTEINFOW {
    DWORD cbSize;
    ULONG fMask;
    HWND hwnd;
    LPCWSTR lpVerb;
    LPCWSTR lpFile;
    LPCWSTR lpParameters;
    LPCWSTR lpDirectory;
    int nShow;
    HINSTANCE hInstApp;
    void *lpIDList;
    LPCWSTR lpClass;
    HKEY hkeyClass;
    DWORD dwHotKey;
    union {
        HANDLE hIcon;
        HANDLE hMonitor;
    };
    HANDLE hProcess;
} SHELLEXECUTEINFOW, *LPSHELLEXECUTEINFOW;

typedef struct _SHELLEXECUTEINFOA {
    DWORD cbSize;
    ULONG fMask;
    HWND hwnd;
    LPCSTR lpVerb;
    LPCSTR lpFile;
    LPCSTR lpParameters;
    LPCSTR lpDirectory;
    int nShow;
    HINSTANCE hInstApp;
    void *lpIDList;
    LPCSTR lpClass;
    HKEY hkeyClass;
    DWORD dwHotKey;
    union {
        HANDLE hIcon;
        HANDLE hMonitor;
    };
    HANDLE hProcess;
} SHELLEXECUTEINFOA, *LPSHELLEXECUTEINFOA;

/* ------------------------------------------------------------------ dropped files */

typedef struct _DROPFILES {
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
} DROPFILES, *LPDROPFILES;

/* ------------------------------------------------------------------ file information */

typedef struct _SHFILEINFOW {
    HICON hIcon;
    int iIcon;
    DWORD dwAttributes;
    WCHAR szDisplayName[MAX_PATH];
    WCHAR szTypeName[80];
} SHFILEINFOW;

typedef struct _SHFILEINFOA {
    HICON hIcon;
    int iIcon;
    DWORD dwAttributes;
    CHAR szDisplayName[MAX_PATH];
    CHAR szTypeName[80];
} SHFILEINFOA;

#define SHGFI_ICON 0x000000100
#define SHGFI_DISPLAYNAME 0x000000200
#define SHGFI_TYPENAME 0x000000400
#define SHGFI_ATTRIBUTES 0x000000800
#define SHGFI_ICONLOCATION 0x000001000
#define SHGFI_EXETYPE 0x000002000
#define SHGFI_SYSICONINDEX 0x000004000
#define SHGFI_LINKOVERLAY 0x000008000
#define SHGFI_SELECTED 0x000010000
#define SHGFI_ATTR_SPECIFIED 0x000020000
#define SHGFI_LARGEICON 0x000000000
#define SHGFI_SMALLICON 0x000000001
#define SHGFI_OPENICON 0x000000002
#define SHGFI_SHELLICONSIZE 0x000000004
#define SHGFI_PIDL 0x000000008
#define SHGFI_USEFILEATTRIBUTES 0x000000010
#define SHGFI_ADDOVERLAYS 0x000000020
#define SHGFI_OVERLAYINDEX 0x000000040

typedef enum SHSTOCKICONID {
    SIID_DOCNOASSOC = 0,
    SIID_DOCASSOC = 1,
    SIID_APPLICATION = 2,
    SIID_FOLDER = 3,
    SIID_FOLDEROPEN = 4,
    SIID_HELP = 23,
    SIID_SHIELD = 77,
    SIID_WARNING = 78,
    SIID_INFO = 79,
    SIID_ERROR = 80,
    SIID_MAX_ICONS = 181,
} SHSTOCKICONID;

#define SHGSI_ICONLOCATION 0
#define SHGSI_ICON SHGFI_ICON
#define SHGSI_SYSICONINDEX SHGFI_SYSICONINDEX
#define SHGSI_LINKOVERLAY SHGFI_LINKOVERLAY
#define SHGSI_SELECTED SHGFI_SELECTED
#define SHGSI_LARGEICON SHGFI_LARGEICON
#define SHGSI_SMALLICON SHGFI_SMALLICON
#define SHGSI_SHELLICONSIZE SHGFI_SHELLICONSIZE

typedef struct _SHSTOCKICONINFO {
    DWORD cbSize;
    HICON hIcon;
    int iSysImageIndex;
    int iIcon;
    WCHAR szPath[MAX_PATH];
} SHSTOCKICONINFO;

/* ------------------------------------------------------------------ file operations */

#define FO_MOVE 0x0001
#define FO_COPY 0x0002
#define FO_DELETE 0x0003
#define FO_RENAME 0x0004

#define FOF_MULTIDESTFILES 0x0001
#define FOF_CONFIRMMOUSE 0x0002
#define FOF_SILENT 0x0004
#define FOF_RENAMEONCOLLISION 0x0008
#define FOF_NOCONFIRMATION 0x0010
#define FOF_WANTMAPPINGHANDLE 0x0020
#define FOF_ALLOWUNDO 0x0040
#define FOF_FILESONLY 0x0080
#define FOF_SIMPLEPROGRESS 0x0100
#define FOF_NOCONFIRMMKDIR 0x0200
#define FOF_NOERRORUI 0x0400
#define FOF_NOCOPYSECURITYATTRIBS 0x0800
#define FOF_NORECURSION 0x1000

typedef WORD FILEOP_FLAGS;
typedef const WCHAR *PCZZWSTR;

typedef const CHAR *PCZZSTR;

typedef struct _SHFILEOPSTRUCTA {
    HWND hwnd;
    UINT wFunc;
    PCZZSTR pFrom;
    PCZZSTR pTo;
    FILEOP_FLAGS fFlags;
    BOOL fAnyOperationsAborted;
    LPVOID hNameMappings;
    PCSTR lpszProgressTitle;
} SHFILEOPSTRUCTA, *LPSHFILEOPSTRUCTA;

typedef struct _SHFILEOPSTRUCTW {
    HWND hwnd;
    UINT wFunc;
    PCZZWSTR pFrom;
    PCZZWSTR pTo;
    FILEOP_FLAGS fFlags;
    BOOL fAnyOperationsAborted;
    LPVOID hNameMappings;
    PCWSTR lpszProgressTitle;
} SHFILEOPSTRUCTW, *LPSHFILEOPSTRUCTW;

/* ------------------------------------------------------------------ notification area */

#define NIM_ADD 0x00000000
#define NIM_MODIFY 0x00000001
#define NIM_DELETE 0x00000002
#define NIM_SETFOCUS 0x00000003
#define NIM_SETVERSION 0x00000004

#define NIF_MESSAGE 0x00000001
#define NIF_ICON 0x00000002
#define NIF_TIP 0x00000004
#define NIF_STATE 0x00000008
#define NIF_INFO 0x00000010
#define NIF_GUID 0x00000020
#define NIF_REALTIME 0x00000040
#define NIF_SHOWTIP 0x00000080

#define NIS_HIDDEN 0x00000001
#define NIS_SHAREDICON 0x00000002

#define NIIF_NONE 0x00000000
#define NIIF_INFO 0x00000001
#define NIIF_WARNING 0x00000002
#define NIIF_ERROR 0x00000003
#define NIIF_USER 0x00000004
#define NIIF_ICON_MASK 0x0000000F
#define NIIF_NOSOUND 0x00000010
#define NIIF_LARGE_ICON 0x00000020
#define NIIF_RESPECT_QUIET_TIME 0x00000080

#define NOTIFYICON_VERSION 3
#define NOTIFYICON_VERSION_4 4

/* what a notification icon's callback message carries as its event, in version 3 and later. */
#define NIN_SELECT (WM_USER + 0)
#define NINF_KEY 0x1
#define NIN_KEYSELECT (NIN_SELECT | NINF_KEY)
#define NIN_BALLOONSHOW (WM_USER + 2)
#define NIN_BALLOONHIDE (WM_USER + 3)
#define NIN_BALLOONTIMEOUT (WM_USER + 4)
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#define NIN_POPUPOPEN (WM_USER + 6)
#define NIN_POPUPCLOSE (WM_USER + 7)

typedef struct _NOTIFYICONDATAW {
    DWORD cbSize;
    HWND hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    HICON hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    union {
        UINT uTimeout;
        UINT uVersion;
    };
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    HICON hBalloonIcon;
} NOTIFYICONDATAW, *PNOTIFYICONDATAW;

typedef struct _NOTIFYICONDATAA {
    DWORD cbSize;
    HWND hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    HICON hIcon;
    CHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    CHAR szInfo[256];
    union {
        UINT uTimeout;
        UINT uVersion;
    };
    CHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    HICON hBalloonIcon;
} NOTIFYICONDATAA, *PNOTIFYICONDATAA;

typedef struct _NOTIFYICONIDENTIFIER {
    DWORD cbSize;
    HWND hWnd;
    UINT uID;
    GUID guidItem;
} NOTIFYICONIDENTIFIER;

/* ------------------------------------------------------------------ app bars */

#define ABM_NEW 0x00000000
#define ABM_REMOVE 0x00000001
#define ABM_QUERYPOS 0x00000002
#define ABM_SETPOS 0x00000003
#define ABM_GETSTATE 0x00000004
#define ABM_GETTASKBARPOS 0x00000005
#define ABM_ACTIVATE 0x00000006
#define ABM_GETAUTOHIDEBAR 0x00000007
#define ABM_SETAUTOHIDEBAR 0x00000008
#define ABM_WINDOWPOSCHANGED 0x0000009
#define ABM_SETSTATE 0x0000000A
#define ABM_GETAUTOHIDEBAREX 0x0000000B
#define ABM_SETAUTOHIDEBAREX 0x0000000C

#define ABS_AUTOHIDE 0x0000001
#define ABS_ALWAYSONTOP 0x0000002

#define ABE_LEFT 0
#define ABE_TOP 1
#define ABE_RIGHT 2
#define ABE_BOTTOM 3

typedef struct _AppBarData {
    DWORD cbSize;
    HWND hWnd;
    UINT uCallbackMessage;
    UINT uEdge;
    RECT rc;
    LPARAM lParam;
} APPBARDATA, *PAPPBARDATA;

/* ------------------------------------------------------------------ shell32 functions */

SHSTDAPI HINSTANCE WINAPI ShellExecuteW(HWND hwnd, LPCWSTR verb, LPCWSTR file, LPCWSTR parameters,
                                        LPCWSTR directory, INT show);
SHSTDAPI HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR verb, LPCSTR file, LPCSTR parameters, LPCSTR directory,
                                        INT show);
SHSTDAPI BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW *info);
SHSTDAPI BOOL WINAPI ShellExecuteExA(SHELLEXECUTEINFOA *info);
SHSTDAPI BOOL WINAPI Shell_NotifyIconA(DWORD message, PNOTIFYICONDATAA data);
SHSTDAPI UINT WINAPI ExtractIconExW(LPCWSTR file, int index, HICON *large, HICON *small, UINT count);
SHSTDAPI UINT WINAPI ExtractIconExA(LPCSTR file, int index, HICON *large, HICON *small, UINT count);

#define OAIF_ALLOW_REGISTRATION 0x00000001
#define OAIF_REGISTER_EXT 0x00000002
#define OAIF_EXEC 0x00000004
#define OAIF_FORCE_REGISTRATION 0x00000008
#define OAIF_HIDE_REGISTRATION 0x00000020
#define OAIF_URL_PROTOCOL 0x00000040
#define OAIF_FILE_IS_URI 0x00000080
typedef struct tagOPENASINFO {
    LPCWSTR pcszFile;
    LPCWSTR pcszClass;
    DWORD oaifInFlags;
} OPENASINFO;
SHSTDAPI HRESULT WINAPI SHOpenWithDialog(HWND parent, const OPENASINFO *info);
SHSTDAPI LPWSTR *WINAPI CommandLineToArgvW(LPCWSTR command_line, int *count);
SHSTDAPI UINT WINAPI DragQueryFileW(HDROP drop, UINT index, LPWSTR file, UINT chars);
SHSTDAPI UINT WINAPI DragQueryFileA(HDROP drop, UINT index, LPSTR file, UINT chars);
SHSTDAPI BOOL WINAPI DragQueryPoint(HDROP drop, POINT *pt);
SHSTDAPI void WINAPI DragFinish(HDROP drop);
SHSTDAPI void WINAPI DragAcceptFiles(HWND hwnd, BOOL accept);
SHSTDAPI DWORD_PTR WINAPI SHGetFileInfoW(LPCWSTR path, DWORD attributes, SHFILEINFOW *info, UINT size, UINT flags);
SHSTDAPI DWORD_PTR WINAPI SHGetFileInfoA(LPCSTR path, DWORD attributes, SHFILEINFOA *info, UINT size, UINT flags);
SHSTDAPI HRESULT WINAPI SHGetStockIconInfo(SHSTOCKICONID id, UINT flags, SHSTOCKICONINFO *info);
SHSTDAPI int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW operation);
SHSTDAPI int WINAPI SHFileOperationA(LPSHFILEOPSTRUCTA operation);
SHSTDAPI BOOL WINAPI Shell_NotifyIconW(DWORD message, PNOTIFYICONDATAW data);
SHSTDAPI HRESULT WINAPI Shell_NotifyIconGetRect(const NOTIFYICONIDENTIFIER *identifier, RECT *rect);
SHSTDAPI UINT_PTR WINAPI SHAppBarMessage(DWORD message, PAPPBARDATA data);

#ifdef __i386__
#pragma pack(pop)
#endif

#endif
