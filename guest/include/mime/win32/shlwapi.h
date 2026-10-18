/*
 * shell light weight utilities, shlwapi.dll: paths, strings, urls, the registry helpers,
 * associations and the small com helpers. kernelbase and shcore host many of them.
 */
#ifndef MIME_WIN32_SHLWAPI_H
#define MIME_WIN32_SHLWAPI_H

#include <mime/win32/objbase.h>
#include <mime/win32/objidl.h>
#include <mime/win32/winuser.h>

#ifndef LWSTDAPI
#define LWSTDAPI DECLSPEC_IMPORT
#endif

/* ------------------------------------------------------------------ paths */

#define GCT_INVALID 0x0000
#define GCT_LFNCHAR 0x0001
#define GCT_SHORTCHAR 0x0002
#define GCT_WILD 0x0004
#define GCT_SEPARATOR 0x0008

#define PIVC_ALLOW_QUESTIONMARK 0x00000001
#define PIVC_ALLOW_STAR 0x00000002
#define PIVC_ALLOW_DOT 0x00000004
#define PIVC_ALLOW_SLASH 0x00000008
#define PIVC_ALLOW_COLON 0x00000010
#define PIVC_ALLOW_SEMICOLON 0x00000020
#define PIVC_ALLOW_COMMA 0x00000040
#define PIVC_ALLOW_SPACE 0x00000080
#define PIVC_ALLOW_NONALPHA 0x00000100
#define PIVC_ALLOW_QUOTE 0x00000200

#define PMSF_NORMAL 0x00000000
#define PMSF_MULTIPLE 0x00000001
#define PMSF_DONT_STRIP_SPACES 0x00010000

#define URL_E_INVALID_SYNTAX ((HRESULT)0x80041001L)

#define WHICH_PIF 0x0001
#define WHICH_COM 0x0002
#define WHICH_EXE 0x0004
#define WHICH_BAT 0x0008
#define WHICH_LNK 0x0010
#define WHICH_CMD 0x0020
#define WHICH_OPTIONAL 0x0040
#define WHICH_DEFAULT (WHICH_PIF | WHICH_COM | WHICH_EXE | WHICH_BAT | WHICH_LNK | WHICH_CMD)

#define PATHCCH_MAX_CCH 0x8000
#define PATHCCH_NONE 0x00000000
#define PATHCCH_ALLOW_LONG_PATHS 0x00000001
#define PATHCCH_FORCE_ENABLE_LONG_NAME_PROCESS 0x00000002
#define PATHCCH_FORCE_DISABLE_LONG_NAME_PROCESS 0x00000004
#define PATHCCH_DO_NOT_NORMALIZE_SEGMENTS 0x00000008
#define PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH 0x00000010
#define PATHCCH_ENSURE_TRAILING_SLASH 0x00000020
#define PATHCCH_CANONICALIZE_SLASHES 0x00000040

LWSTDAPI LPWSTR WINAPI PathAddBackslashW(LPWSTR path);
LWSTDAPI LPSTR WINAPI PathAddBackslashA(LPSTR path);
LWSTDAPI BOOL WINAPI PathAddExtensionW(LPWSTR path, LPCWSTR extension);
LWSTDAPI BOOL WINAPI PathAddExtensionA(LPSTR path, LPCSTR extension);
LWSTDAPI BOOL WINAPI PathAppendW(LPWSTR path, LPCWSTR more);
LWSTDAPI BOOL WINAPI PathAppendA(LPSTR path, LPCSTR more);
LWSTDAPI LPWSTR WINAPI PathBuildRootW(LPWSTR root, int drive);
LWSTDAPI LPSTR WINAPI PathBuildRootA(LPSTR root, int drive);
LWSTDAPI BOOL WINAPI PathCanonicalizeW(LPWSTR out, LPCWSTR path);
LWSTDAPI BOOL WINAPI PathCanonicalizeA(LPSTR out, LPCSTR path);
LWSTDAPI LPWSTR WINAPI PathCombineW(LPWSTR out, LPCWSTR directory, LPCWSTR file);
LWSTDAPI LPSTR WINAPI PathCombineA(LPSTR out, LPCSTR directory, LPCSTR file);
LWSTDAPI int WINAPI PathCommonPrefixW(LPCWSTR path1, LPCWSTR path2, LPWSTR common);
LWSTDAPI int WINAPI PathCommonPrefixA(LPCSTR path1, LPCSTR path2, LPSTR common);
LWSTDAPI BOOL WINAPI PathCompactPathW(HDC dc, LPWSTR path, UINT width);
LWSTDAPI BOOL WINAPI PathCompactPathA(HDC dc, LPSTR path, UINT width);
LWSTDAPI BOOL WINAPI PathCompactPathExW(LPWSTR out, LPCWSTR path, UINT max, DWORD flags);
LWSTDAPI BOOL WINAPI PathCompactPathExA(LPSTR out, LPCSTR path, UINT max, DWORD flags);
LWSTDAPI HRESULT WINAPI PathCreateFromUrlW(LPCWSTR url, LPWSTR path, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI PathCreateFromUrlA(LPCSTR url, LPSTR path, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI PathCreateFromUrlAlloc(LPCWSTR url, LPWSTR *path, DWORD flags);
LWSTDAPI BOOL WINAPI PathFileExistsW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathFileExistsA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathFileExistsAndAttributesW(LPCWSTR path, DWORD *attributes);
LWSTDAPI BOOL WINAPI PathFileExistsAndAttributesA(LPCSTR path, DWORD *attributes);
LWSTDAPI BOOL WINAPI PathFileExistsDefExtW(LPWSTR path, DWORD which);
LWSTDAPI BOOL WINAPI PathFileExistsDefExtA(LPSTR path, DWORD which);
LWSTDAPI LPWSTR WINAPI PathFindExtensionW(LPCWSTR path);
LWSTDAPI LPSTR WINAPI PathFindExtensionA(LPCSTR path);
LWSTDAPI LPWSTR WINAPI PathFindFileNameW(LPCWSTR path);
LWSTDAPI LPSTR WINAPI PathFindFileNameA(LPCSTR path);
LWSTDAPI LPWSTR WINAPI PathFindNextComponentW(LPCWSTR path);
LWSTDAPI LPSTR WINAPI PathFindNextComponentA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathFindOnPathW(LPWSTR file, LPCWSTR *directories);
LWSTDAPI BOOL WINAPI PathFindOnPathA(LPSTR file, LPCSTR *directories);
LWSTDAPI BOOL WINAPI PathFindOnPathExW(LPWSTR file, LPCWSTR *directories, DWORD which);
LWSTDAPI BOOL WINAPI PathFindOnPathExA(LPSTR file, LPCSTR *directories, DWORD which);
LWSTDAPI LPCWSTR WINAPI PathFindSuffixArrayW(LPCWSTR path, const LPCWSTR *suffixes, int count);
LWSTDAPI LPCSTR WINAPI PathFindSuffixArrayA(LPCSTR path, const LPCSTR *suffixes, int count);
LWSTDAPI LPWSTR WINAPI PathGetArgsW(LPCWSTR path);
LWSTDAPI LPSTR WINAPI PathGetArgsA(LPCSTR path);
LWSTDAPI UINT WINAPI PathGetCharTypeW(WCHAR c);
LWSTDAPI UINT WINAPI PathGetCharTypeA(UCHAR c);
LWSTDAPI int WINAPI PathGetDriveNumberW(LPCWSTR path);
LWSTDAPI int WINAPI PathGetDriveNumberA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsContentTypeW(LPCWSTR path, LPCWSTR type);
LWSTDAPI BOOL WINAPI PathIsContentTypeA(LPCSTR path, LPCSTR type);
LWSTDAPI BOOL WINAPI PathIsDirectoryW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsDirectoryA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsDirectoryEmptyW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsDirectoryEmptyA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsFileSpecW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsFileSpecA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsLFNFileSpecW(LPCWSTR name);
LWSTDAPI BOOL WINAPI PathIsLFNFileSpecA(LPCSTR name);
LWSTDAPI BOOL WINAPI PathIsNetworkPathW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsNetworkPathA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsPrefixW(LPCWSTR prefix, LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsPrefixA(LPCSTR prefix, LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsRelativeW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsRelativeA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsRootW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsRootA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsSameRootW(LPCWSTR path1, LPCWSTR path2);
LWSTDAPI BOOL WINAPI PathIsSameRootA(LPCSTR path1, LPCSTR path2);
LWSTDAPI BOOL WINAPI PathIsSystemFolderW(LPCWSTR path, DWORD attributes);
LWSTDAPI BOOL WINAPI PathIsSystemFolderA(LPCSTR path, DWORD attributes);
LWSTDAPI BOOL WINAPI PathIsUNCW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsUNCA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsUNCServerW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsUNCServerA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsUNCServerShareW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsUNCServerShareA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsURLW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathIsURLA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathIsValidCharW(WCHAR c, DWORD flags);
LWSTDAPI BOOL WINAPI PathIsValidCharA(UCHAR c, DWORD flags);
LWSTDAPI BOOL WINAPI PathMakePrettyW(LPWSTR path);
LWSTDAPI BOOL WINAPI PathMakePrettyA(LPSTR path);
LWSTDAPI BOOL WINAPI PathMakeSystemFolderW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathMakeSystemFolderA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathMatchSpecW(LPCWSTR file, LPCWSTR spec);
LWSTDAPI BOOL WINAPI PathMatchSpecA(LPCSTR file, LPCSTR spec);
LWSTDAPI HRESULT WINAPI PathMatchSpecExW(LPCWSTR file, LPCWSTR spec, DWORD flags);
LWSTDAPI HRESULT WINAPI PathMatchSpecExA(LPCSTR file, LPCSTR spec, DWORD flags);
LWSTDAPI int WINAPI PathParseIconLocationW(LPWSTR location);
LWSTDAPI int WINAPI PathParseIconLocationA(LPSTR location);
LWSTDAPI BOOL WINAPI PathQuoteSpacesW(LPWSTR path);
LWSTDAPI BOOL WINAPI PathQuoteSpacesA(LPSTR path);
LWSTDAPI BOOL WINAPI PathRelativePathToW(LPWSTR path, LPCWSTR from, DWORD from_attributes, LPCWSTR to,
                                         DWORD to_attributes);
LWSTDAPI BOOL WINAPI PathRelativePathToA(LPSTR path, LPCSTR from, DWORD from_attributes, LPCSTR to,
                                         DWORD to_attributes);
LWSTDAPI void WINAPI PathRemoveArgsW(LPWSTR path);
LWSTDAPI void WINAPI PathRemoveArgsA(LPSTR path);
LWSTDAPI LPWSTR WINAPI PathRemoveBackslashW(LPWSTR path);
LWSTDAPI LPSTR WINAPI PathRemoveBackslashA(LPSTR path);
LWSTDAPI void WINAPI PathRemoveBlanksW(LPWSTR path);
LWSTDAPI void WINAPI PathRemoveBlanksA(LPSTR path);
LWSTDAPI void WINAPI PathRemoveExtensionW(LPWSTR path);
LWSTDAPI void WINAPI PathRemoveExtensionA(LPSTR path);
LWSTDAPI BOOL WINAPI PathRemoveFileSpecW(LPWSTR path);
LWSTDAPI BOOL WINAPI PathRemoveFileSpecA(LPSTR path);
LWSTDAPI BOOL WINAPI PathRenameExtensionW(LPWSTR path, LPCWSTR extension);
LWSTDAPI BOOL WINAPI PathRenameExtensionA(LPSTR path, LPCSTR extension);
LWSTDAPI BOOL WINAPI PathSearchAndQualifyW(LPCWSTR path, LPWSTR out, UINT size);
LWSTDAPI BOOL WINAPI PathSearchAndQualifyA(LPCSTR path, LPSTR out, UINT size);
LWSTDAPI void WINAPI PathSetDlgItemPathW(HWND dialog, int id, LPCWSTR path);
LWSTDAPI void WINAPI PathSetDlgItemPathA(HWND dialog, int id, LPCSTR path);
LWSTDAPI LPWSTR WINAPI PathSkipRootW(LPCWSTR path);
LWSTDAPI LPSTR WINAPI PathSkipRootA(LPCSTR path);
LWSTDAPI void WINAPI PathStripPathW(LPWSTR path);
LWSTDAPI void WINAPI PathStripPathA(LPSTR path);
LWSTDAPI BOOL WINAPI PathStripToRootW(LPWSTR path);
LWSTDAPI BOOL WINAPI PathStripToRootA(LPSTR path);
LWSTDAPI BOOL WINAPI PathUnExpandEnvStringsW(LPCWSTR path, LPWSTR out, UINT size);
LWSTDAPI BOOL WINAPI PathUnExpandEnvStringsA(LPCSTR path, LPSTR out, UINT size);
LWSTDAPI void WINAPI PathUndecorateW(LPWSTR path);
LWSTDAPI void WINAPI PathUndecorateA(LPSTR path);
LWSTDAPI BOOL WINAPI PathUnmakeSystemFolderW(LPCWSTR path);
LWSTDAPI BOOL WINAPI PathUnmakeSystemFolderA(LPCSTR path);
LWSTDAPI BOOL WINAPI PathUnquoteSpacesW(LPWSTR path);
LWSTDAPI BOOL WINAPI PathUnquoteSpacesA(LPSTR path);
LWSTDAPI DWORD WINAPI SHExpandEnvironmentStringsW(LPCWSTR source, LPWSTR out, DWORD size);
LWSTDAPI DWORD WINAPI SHExpandEnvironmentStringsA(LPCSTR source, LPSTR out, DWORD size);

LWSTDAPI HRESULT WINAPI PathCchAddBackslash(LPWSTR path, SIZE_T size);
LWSTDAPI HRESULT WINAPI PathCchAddBackslashEx(LPWSTR path, SIZE_T size, LPWSTR *end, SIZE_T *remaining);
LWSTDAPI HRESULT WINAPI PathCchAddExtension(LPWSTR path, SIZE_T size, LPCWSTR extension);
LWSTDAPI HRESULT WINAPI PathCchAppend(LPWSTR path, SIZE_T size, LPCWSTR more);
LWSTDAPI HRESULT WINAPI PathCchAppendEx(LPWSTR path, SIZE_T size, LPCWSTR more, ULONG flags);
LWSTDAPI HRESULT WINAPI PathCchCanonicalize(LPWSTR out, SIZE_T size, LPCWSTR path);
LWSTDAPI HRESULT WINAPI PathCchCanonicalizeEx(LPWSTR out, SIZE_T size, LPCWSTR path, ULONG flags);
LWSTDAPI HRESULT WINAPI PathCchCombine(LPWSTR out, SIZE_T size, LPCWSTR path, LPCWSTR more);
LWSTDAPI HRESULT WINAPI PathCchCombineEx(LPWSTR out, SIZE_T size, LPCWSTR path, LPCWSTR more, ULONG flags);
LWSTDAPI HRESULT WINAPI PathCchFindExtension(LPCWSTR path, SIZE_T size, LPCWSTR *extension);
LWSTDAPI BOOL WINAPI PathCchIsRoot(LPCWSTR path);
LWSTDAPI HRESULT WINAPI PathCchRemoveBackslash(LPWSTR path, SIZE_T size);
LWSTDAPI HRESULT WINAPI PathCchRemoveBackslashEx(LPWSTR path, SIZE_T size, LPWSTR *end, SIZE_T *remaining);
LWSTDAPI HRESULT WINAPI PathCchRemoveExtension(LPWSTR path, SIZE_T size);
LWSTDAPI HRESULT WINAPI PathCchRemoveFileSpec(LPWSTR path, SIZE_T size);
LWSTDAPI HRESULT WINAPI PathCchRenameExtension(LPWSTR path, SIZE_T size, LPCWSTR extension);
LWSTDAPI HRESULT WINAPI PathCchSkipRoot(LPCWSTR path, LPCWSTR *root_end);
LWSTDAPI HRESULT WINAPI PathCchStripPrefix(LPWSTR path, SIZE_T size);
LWSTDAPI HRESULT WINAPI PathCchStripToRoot(LPWSTR path, SIZE_T size);
LWSTDAPI HRESULT WINAPI PathAllocCanonicalize(LPCWSTR path, ULONG flags, LPWSTR *out);
LWSTDAPI HRESULT WINAPI PathAllocCombine(LPCWSTR path, LPCWSTR more, ULONG flags, LPWSTR *out);
LWSTDAPI BOOL WINAPI PathIsUNCEx(LPCWSTR path, LPCWSTR *server);

/* ------------------------------------------------------------------ strings */

#define STIF_DEFAULT 0x00000000
#define STIF_SUPPORT_HEX 0x00000001

#define STR_MAX 2048

typedef enum {
    SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT = 0x0001,
    SFBS_FLAGS_TRUNCATE_UNDISPLAYED_DECIMAL_DIGITS = 0x0002,
} SFBS_FLAGS;

LWSTDAPI BOOL WINAPI ChrCmpIW(WCHAR c1, WCHAR c2);
LWSTDAPI BOOL WINAPI ChrCmpIA(WORD c1, WORD c2);
LWSTDAPI BOOL WINAPI IsCharSpaceW(WCHAR c);
LWSTDAPI BOOL WINAPI IsCharSpaceA(CHAR c);
LWSTDAPI BOOL WINAPI IsCharBlankW(WCHAR c);
LWSTDAPI BOOL WINAPI IsCharCntrlW(WCHAR c);
LWSTDAPI BOOL WINAPI IsCharDigitW(WCHAR c);
LWSTDAPI BOOL WINAPI IsCharPunctW(WCHAR c);
LWSTDAPI BOOL WINAPI IsCharXDigitW(WCHAR c);
LWSTDAPI int WINAPI StrCSpnW(LPCWSTR string, LPCWSTR set);
LWSTDAPI int WINAPI StrCSpnA(LPCSTR string, LPCSTR set);
LWSTDAPI int WINAPI StrCSpnIW(LPCWSTR string, LPCWSTR set);
LWSTDAPI int WINAPI StrCSpnIA(LPCSTR string, LPCSTR set);
LWSTDAPI LPWSTR WINAPI StrCatBuffW(LPWSTR dest, LPCWSTR src, int size);
LWSTDAPI LPSTR WINAPI StrCatBuffA(LPSTR dest, LPCSTR src, int size);
LWSTDAPI DWORD WINAPI StrCatChainW(LPWSTR dest, DWORD size, DWORD at, LPCWSTR src);
LWSTDAPI LPWSTR WINAPI StrCatW(LPWSTR dest, LPCWSTR src);
LWSTDAPI LPWSTR WINAPI StrChrW(LPCWSTR string, WCHAR c);
LWSTDAPI LPSTR WINAPI StrChrA(LPCSTR string, WORD c);
LWSTDAPI LPWSTR WINAPI StrChrIW(LPCWSTR string, WCHAR c);
LWSTDAPI LPSTR WINAPI StrChrIA(LPCSTR string, WORD c);
LWSTDAPI LPWSTR WINAPI StrChrNW(LPCWSTR string, WCHAR c, UINT max);
LWSTDAPI LPWSTR WINAPI StrChrNIW(LPCWSTR string, WCHAR c, UINT max);
LWSTDAPI int WINAPI StrCmpW(LPCWSTR s1, LPCWSTR s2);
LWSTDAPI int WINAPI StrCmpIW(LPCWSTR s1, LPCWSTR s2);
LWSTDAPI int WINAPI StrCmpCW(LPCWSTR s1, LPCWSTR s2);
LWSTDAPI int WINAPI StrCmpCA(LPCSTR s1, LPCSTR s2);
LWSTDAPI int WINAPI StrCmpICW(LPCWSTR s1, LPCWSTR s2);
LWSTDAPI int WINAPI StrCmpICA(LPCSTR s1, LPCSTR s2);
LWSTDAPI int WINAPI StrCmpLogicalW(LPCWSTR s1, LPCWSTR s2);
LWSTDAPI int WINAPI StrCmpNW(LPCWSTR s1, LPCWSTR s2, int count);
LWSTDAPI int WINAPI StrCmpNA(LPCSTR s1, LPCSTR s2, int count);
LWSTDAPI int WINAPI StrCmpNIW(LPCWSTR s1, LPCWSTR s2, int count);
LWSTDAPI int WINAPI StrCmpNIA(LPCSTR s1, LPCSTR s2, int count);
LWSTDAPI int WINAPI StrCmpNCW(LPCWSTR s1, LPCWSTR s2, int count);
LWSTDAPI int WINAPI StrCmpNCA(LPCSTR s1, LPCSTR s2, int count);
LWSTDAPI int WINAPI StrCmpNICW(LPCWSTR s1, LPCWSTR s2, int count);
LWSTDAPI int WINAPI StrCmpNICA(LPCSTR s1, LPCSTR s2, int count);
LWSTDAPI LPWSTR WINAPI StrCpyW(LPWSTR dest, LPCWSTR src);
LWSTDAPI LPWSTR WINAPI StrCpyNW(LPWSTR dest, LPCWSTR src, int size);
LWSTDAPI LPWSTR WINAPI StrCpyNXW(LPWSTR dest, LPCWSTR src, int size);
LWSTDAPI LPSTR WINAPI StrCpyNXA(LPSTR dest, LPCSTR src, int size);
LWSTDAPI LPWSTR WINAPI StrDupW(LPCWSTR string);
LWSTDAPI LPSTR WINAPI StrDupA(LPCSTR string);
LWSTDAPI HRESULT WINAPI StrFormatByteSizeEx(ULONGLONG size, SFBS_FLAGS flags, LPWSTR out, UINT length);
LWSTDAPI LPWSTR WINAPI StrFormatByteSizeW(LONGLONG size, LPWSTR out, UINT length);
LWSTDAPI LPSTR WINAPI StrFormatByteSizeA(DWORD size, LPSTR out, UINT length);
LWSTDAPI LPSTR WINAPI StrFormatByteSize64A(LONGLONG size, LPSTR out, UINT length);
LWSTDAPI LPWSTR WINAPI StrFormatKBSizeW(LONGLONG size, LPWSTR out, UINT length);
LWSTDAPI LPSTR WINAPI StrFormatKBSizeA(LONGLONG size, LPSTR out, UINT length);
LWSTDAPI int WINAPI StrFromTimeIntervalW(LPWSTR out, UINT length, DWORD milliseconds, int digits);
LWSTDAPI int WINAPI StrFromTimeIntervalA(LPSTR out, UINT length, DWORD milliseconds, int digits);
LWSTDAPI BOOL WINAPI StrIsIntlEqualW(BOOL case_sensitive, LPCWSTR s1, LPCWSTR s2, int count);
LWSTDAPI BOOL WINAPI StrIsIntlEqualA(BOOL case_sensitive, LPCSTR s1, LPCSTR s2, int count);
LWSTDAPI LPWSTR WINAPI StrNCatW(LPWSTR dest, LPCWSTR src, int count);
LWSTDAPI LPSTR WINAPI StrNCatA(LPSTR dest, LPCSTR src, int count);
LWSTDAPI LPWSTR WINAPI StrPBrkW(LPCWSTR string, LPCWSTR set);
LWSTDAPI LPSTR WINAPI StrPBrkA(LPCSTR string, LPCSTR set);
LWSTDAPI LPWSTR WINAPI StrRChrW(LPCWSTR string, LPCWSTR end, WCHAR c);
LWSTDAPI LPSTR WINAPI StrRChrA(LPCSTR string, LPCSTR end, WORD c);
LWSTDAPI LPWSTR WINAPI StrRChrIW(LPCWSTR string, LPCWSTR end, WCHAR c);
LWSTDAPI LPSTR WINAPI StrRChrIA(LPCSTR string, LPCSTR end, WORD c);
LWSTDAPI LPWSTR WINAPI StrRStrIW(LPCWSTR string, LPCWSTR end, LPCWSTR search);
LWSTDAPI LPSTR WINAPI StrRStrIA(LPCSTR string, LPCSTR end, LPCSTR search);
LWSTDAPI int WINAPI StrSpnW(LPCWSTR string, LPCWSTR set);
LWSTDAPI int WINAPI StrSpnA(LPCSTR string, LPCSTR set);
LWSTDAPI LPWSTR WINAPI StrStrW(LPCWSTR string, LPCWSTR search);
LWSTDAPI LPSTR WINAPI StrStrA(LPCSTR string, LPCSTR search);
LWSTDAPI LPWSTR WINAPI StrStrIW(LPCWSTR string, LPCWSTR search);
LWSTDAPI LPSTR WINAPI StrStrIA(LPCSTR string, LPCSTR search);
LWSTDAPI LPWSTR WINAPI StrStrNW(LPCWSTR string, LPCWSTR search, UINT max);
LWSTDAPI LPWSTR WINAPI StrStrNIW(LPCWSTR string, LPCWSTR search, UINT max);
LWSTDAPI int WINAPI StrToIntW(LPCWSTR string);
LWSTDAPI int WINAPI StrToIntA(LPCSTR string);
LWSTDAPI BOOL WINAPI StrToIntExW(LPCWSTR string, DWORD flags, int *value);
LWSTDAPI BOOL WINAPI StrToIntExA(LPCSTR string, DWORD flags, int *value);
LWSTDAPI BOOL WINAPI StrToInt64ExW(LPCWSTR string, DWORD flags, LONGLONG *value);
LWSTDAPI BOOL WINAPI StrToInt64ExA(LPCSTR string, DWORD flags, LONGLONG *value);
LWSTDAPI BOOL WINAPI StrTrimW(LPWSTR string, LPCWSTR trim);
LWSTDAPI BOOL WINAPI StrTrimA(LPSTR string, LPCSTR trim);
LWSTDAPI int WINAPI SHTruncateString(LPSTR string, UINT size);
LWSTDAPI HRESULT WINAPI SHLoadIndirectString(LPCWSTR source, LPWSTR out, UINT size, void **reserved);
LWSTDAPI HRESULT WINAPI SHStrDupW(LPCWSTR string, LPWSTR *copy);
LWSTDAPI HRESULT WINAPI SHStrDupA(LPCSTR string, LPWSTR *copy);
LWSTDAPI int WINAPI SHAnsiToUnicode(LPCSTR source, LPWSTR out, int size);
LWSTDAPI int WINAPI SHUnicodeToAnsi(LPCWSTR source, LPSTR out, int size);
LWSTDAPI int WINAPI SHAnsiToAnsi(LPCSTR source, LPSTR out, int size);
LWSTDAPI int WINAPI SHUnicodeToUnicode(LPCWSTR source, LPWSTR out, int size);
LWSTDAPI int WINAPI SHAnsiToUnicodeCP(UINT code_page, LPCSTR source, LPWSTR out, int size);
LWSTDAPI int WINAPI SHUnicodeToAnsiCP(UINT code_page, LPCWSTR source, LPSTR out, int size);
LWSTDAPI int WINAPIV wnsprintfW(LPWSTR out, int size, LPCWSTR format, ...);
LWSTDAPI int WINAPIV wnsprintfA(LPSTR out, int size, LPCSTR format, ...);
LWSTDAPI int WINAPI wvnsprintfW(LPWSTR out, int size, LPCWSTR format, va_list args);
LWSTDAPI int WINAPI wvnsprintfA(LPSTR out, int size, LPCSTR format, va_list args);

#define StrCmpIA lstrcmpiA
#define StrCmpA lstrcmpA
#define StrCpyA lstrcpyA
#define StrCpyNA lstrcpynA
#define StrCatA lstrcatA

/* ------------------------------------------------------------------ item strings */

#define STRRET_WSTR 0x0000
#define STRRET_OFFSET 0x0001
#define STRRET_CSTR 0x0002

#ifndef MIME_STRRET_DEFINED
#define MIME_STRRET_DEFINED
typedef struct _STRRET {
    UINT uType;
    union {
        LPWSTR pOleStr;
        UINT uOffset;
        char cStr[MAX_PATH];
    };
} STRRET, *LPSTRRET;
#endif

struct _ITEMIDLIST;
LWSTDAPI HRESULT WINAPI StrRetToStrW(STRRET *strret, const struct _ITEMIDLIST *pidl, LPWSTR *out);
LWSTDAPI HRESULT WINAPI StrRetToStrA(STRRET *strret, const struct _ITEMIDLIST *pidl, LPSTR *out);
LWSTDAPI HRESULT WINAPI StrRetToBufW(STRRET *strret, const struct _ITEMIDLIST *pidl, LPWSTR out, UINT size);
LWSTDAPI HRESULT WINAPI StrRetToBufA(STRRET *strret, const struct _ITEMIDLIST *pidl, LPSTR out, UINT size);
LWSTDAPI HRESULT WINAPI StrRetToBSTR(STRRET *strret, const struct _ITEMIDLIST *pidl, WCHAR **out);

/* ------------------------------------------------------------------ dates and times */

#define FDTF_SHORTTIME 0x00000001
#define FDTF_SHORTDATE 0x00000002
#define FDTF_DEFAULT (FDTF_SHORTDATE | FDTF_SHORTTIME)
#define FDTF_LONGDATE 0x00000004
#define FDTF_LONGTIME 0x00000008
#define FDTF_RELATIVE 0x00000010
#define FDTF_LTRDATE 0x00000100
#define FDTF_RTLDATE 0x00000200
#define FDTF_NOAUTOREADINGORDER 0x00000400

LWSTDAPI int WINAPI SHFormatDateTimeW(const FILETIME *time, DWORD *flags, LPWSTR out, UINT size);
LWSTDAPI int WINAPI SHFormatDateTimeA(const FILETIME *time, DWORD *flags, LPSTR out, UINT size);

/* ------------------------------------------------------------------ urls */

typedef enum {
    URL_SCHEME_INVALID = -1,
    URL_SCHEME_UNKNOWN = 0,
    URL_SCHEME_FTP,
    URL_SCHEME_HTTP,
    URL_SCHEME_GOPHER,
    URL_SCHEME_MAILTO,
    URL_SCHEME_NEWS,
    URL_SCHEME_NNTP,
    URL_SCHEME_TELNET,
    URL_SCHEME_WAIS,
    URL_SCHEME_FILE,
    URL_SCHEME_MK,
    URL_SCHEME_HTTPS,
    URL_SCHEME_SHELL,
    URL_SCHEME_SNEWS,
    URL_SCHEME_LOCAL,
    URL_SCHEME_JAVASCRIPT,
    URL_SCHEME_VBSCRIPT,
    URL_SCHEME_ABOUT,
    URL_SCHEME_RES,
    URL_SCHEME_MSSHELLROOTED,
    URL_SCHEME_MSSHELLIDLIST,
    URL_SCHEME_MSHELP,
    URL_SCHEME_MSSHELLDEVICE,
    URL_SCHEME_WILDCARD,
    URL_SCHEME_SEARCH_MS,
    URL_SCHEME_SEARCH,
    URL_SCHEME_KNOWNFOLDER,
    URL_SCHEME_MAXVALUE,
} URL_SCHEME;

typedef enum {
    URL_PART_NONE = 0,
    URL_PART_SCHEME = 1,
    URL_PART_HOSTNAME,
    URL_PART_USERNAME,
    URL_PART_PASSWORD,
    URL_PART_PORT,
    URL_PART_QUERY,
} URL_PART;

typedef enum {
    URLIS_URL,
    URLIS_OPAQUE,
    URLIS_NOHISTORY,
    URLIS_FILEURL,
    URLIS_APPLIABLE,
    URLIS_DIRECTORY,
    URLIS_HASQUERY,
} URLIS;

#define URL_UNESCAPE 0x10000000
#define URL_ESCAPE_UNSAFE 0x20000000
#define URL_PLUGGABLE_PROTOCOL 0x40000000
#define URL_WININET_COMPATIBILITY 0x80000000
#define URL_DONT_ESCAPE_EXTRA_INFO 0x02000000
#define URL_DONT_UNESCAPE_EXTRA_INFO URL_DONT_ESCAPE_EXTRA_INFO
#define URL_BROWSER_MODE URL_DONT_ESCAPE_EXTRA_INFO
#define URL_ESCAPE_SPACES_ONLY 0x04000000
#define URL_DONT_SIMPLIFY 0x08000000
#define URL_NO_META URL_DONT_SIMPLIFY
#define URL_UNESCAPE_INPLACE 0x00100000
#define URL_CONVERT_IF_DOSPATH 0x00200000
#define URL_UNESCAPE_HIGH_ANSI_ONLY 0x00400000
#define URL_INTERNAL_PATH 0x00800000
#define URL_FILE_USE_PATHURL 0x00010000
#define URL_DONT_UNESCAPE 0x00020000
#define URL_ESCAPE_AS_UTF8 0x00040000
#define URL_UNESCAPE_AS_UTF8 URL_ESCAPE_AS_UTF8
#define URL_ESCAPE_ASCII_URI_COMPONENT 0x00080000
#define URL_ESCAPE_URI_COMPONENT (URL_ESCAPE_ASCII_URI_COMPONENT | URL_ESCAPE_AS_UTF8)
#define URL_UNESCAPE_URI_COMPONENT URL_UNESCAPE_AS_UTF8
#define URL_ESCAPE_PERCENT 0x00001000
#define URL_ESCAPE_SEGMENT_ONLY 0x00002000
#define URL_PARTFLAG_KEEPSCHEME 0x00000001
#define URL_APPLY_DEFAULT 0x00000001
#define URL_APPLY_GUESSSCHEME 0x00000002
#define URL_APPLY_GUESSFILE 0x00000004
#define URL_APPLY_FORCEAPPLY 0x00000008

typedef struct tagPARSEDURLA {
    DWORD cbSize;
    LPCSTR pszProtocol;
    UINT cchProtocol;
    LPCSTR pszSuffix;
    UINT cchSuffix;
    UINT nScheme;
} PARSEDURLA, *PPARSEDURLA;

typedef struct tagPARSEDURLW {
    DWORD cbSize;
    LPCWSTR pszProtocol;
    UINT cchProtocol;
    LPCWSTR pszSuffix;
    UINT cchSuffix;
    UINT nScheme;
} PARSEDURLW, *PPARSEDURLW;

LWSTDAPI HRESULT WINAPI ParseURLW(LPCWSTR url, PARSEDURLW *parsed);
LWSTDAPI HRESULT WINAPI ParseURLA(LPCSTR url, PARSEDURLA *parsed);
LWSTDAPI HRESULT WINAPI UrlApplySchemeW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlApplySchemeA(LPCSTR url, LPSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlCanonicalizeW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlCanonicalizeA(LPCSTR url, LPSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlCombineW(LPCWSTR base, LPCWSTR relative, LPWSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlCombineA(LPCSTR base, LPCSTR relative, LPSTR out, DWORD *size, DWORD flags);
LWSTDAPI int WINAPI UrlCompareW(LPCWSTR url1, LPCWSTR url2, BOOL ignore_slash);
LWSTDAPI int WINAPI UrlCompareA(LPCSTR url1, LPCSTR url2, BOOL ignore_slash);
LWSTDAPI HRESULT WINAPI UrlCreateFromPathW(LPCWSTR path, LPWSTR url, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlCreateFromPathA(LPCSTR path, LPSTR url, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlEscapeW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlEscapeA(LPCSTR url, LPSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlFixupW(LPCWSTR url, LPWSTR out, DWORD size);
LWSTDAPI LPCWSTR WINAPI UrlGetLocationW(LPCWSTR url);
LWSTDAPI LPCSTR WINAPI UrlGetLocationA(LPCSTR url);
LWSTDAPI HRESULT WINAPI UrlGetPartW(LPCWSTR url, LPWSTR out, DWORD *size, DWORD part, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlGetPartA(LPCSTR url, LPSTR out, DWORD *size, DWORD part, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlHashW(LPCWSTR url, BYTE *hash, DWORD size);
LWSTDAPI HRESULT WINAPI UrlHashA(LPCSTR url, BYTE *hash, DWORD size);
LWSTDAPI BOOL WINAPI UrlIsW(LPCWSTR url, URLIS what);
LWSTDAPI BOOL WINAPI UrlIsA(LPCSTR url, URLIS what);
LWSTDAPI BOOL WINAPI UrlIsNoHistoryW(LPCWSTR url);
LWSTDAPI BOOL WINAPI UrlIsNoHistoryA(LPCSTR url);
LWSTDAPI BOOL WINAPI UrlIsOpaqueW(LPCWSTR url);
LWSTDAPI BOOL WINAPI UrlIsOpaqueA(LPCSTR url);
LWSTDAPI HRESULT WINAPI UrlUnescapeW(LPWSTR url, LPWSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI UrlUnescapeA(LPSTR url, LPSTR out, DWORD *size, DWORD flags);
LWSTDAPI HRESULT WINAPI HashData(const BYTE *data, DWORD size, BYTE *hash, DWORD hash_size);
LWSTDAPI HRESULT WINAPI GetAcceptLanguagesW(LPWSTR languages, DWORD *size);
LWSTDAPI HRESULT WINAPI GetAcceptLanguagesA(LPSTR languages, DWORD *size);
LWSTDAPI BOOL WINAPI IsInternetESCEnabled(void);

#define UrlEscapeSpaces(url, out, size)                                                                               \
    UrlCanonicalizeW(url, out, size, URL_ESCAPE_SPACES_ONLY | URL_DONT_ESCAPE_EXTRA_INFO)
#define UrlUnescapeInPlace(url, flags) UrlUnescapeW(url, nullptr, nullptr, (flags) | URL_UNESCAPE_INPLACE)

/* ------------------------------------------------------------------ the registry */

#define SRRF_RT_REG_NONE 0x00000001
#define SRRF_RT_REG_SZ 0x00000002
#define SRRF_RT_REG_EXPAND_SZ 0x00000004
#define SRRF_RT_REG_BINARY 0x00000008
#define SRRF_RT_REG_DWORD 0x00000010
#define SRRF_RT_REG_MULTI_SZ 0x00000020
#define SRRF_RT_REG_QWORD 0x00000040
#define SRRF_RT_DWORD (SRRF_RT_REG_BINARY | SRRF_RT_REG_DWORD)
#define SRRF_RT_QWORD (SRRF_RT_REG_BINARY | SRRF_RT_REG_QWORD)
#define SRRF_RT_ANY 0x0000FFFF
#define SRRF_RM_ANY 0x00000000
#define SRRF_RM_NORMAL 0x00010000
#define SRRF_RM_SAFE 0x00020000
#define SRRF_RM_SAFENETWORK 0x00040000
#define SRRF_NOEXPAND 0x10000000
#define SRRF_ZEROONFAILURE 0x20000000
#define SRRF_NOVIRT 0x40000000

typedef enum {
    SHREGDEL_DEFAULT = 0x00000000,
    SHREGDEL_HKCU = 0x00000001,
    SHREGDEL_HKLM = 0x00000010,
    SHREGDEL_BOTH = 0x00000011,
} SHREGDEL_FLAGS;

typedef enum {
    SHREGENUM_DEFAULT = 0x00000000,
    SHREGENUM_HKCU = 0x00000001,
    SHREGENUM_HKLM = 0x00000010,
    SHREGENUM_BOTH = 0x00000011,
} SHREGENUM_FLAGS;

#define SHREGSET_HKCU 0x00000001
#define SHREGSET_FORCE_HKCU 0x00000002
#define SHREGSET_HKLM 0x00000004
#define SHREGSET_FORCE_HKLM 0x00000008
#define SHREGSET_DEFAULT (SHREGSET_FORCE_HKCU | SHREGSET_HKLM)

typedef HANDLE HUSKEY, *PHUSKEY;

LWSTDAPI LONG WINAPI SHGetValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size);
LWSTDAPI LONG WINAPI SHGetValueA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD *type, void *data, DWORD *size);
LWSTDAPI LONG WINAPI SHSetValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD type, const void *data, DWORD size);
LWSTDAPI LONG WINAPI SHSetValueA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD type, const void *data, DWORD size);
LWSTDAPI LONG WINAPI SHDeleteValueW(HKEY key, LPCWSTR subkey, LPCWSTR value);
LWSTDAPI LONG WINAPI SHDeleteValueA(HKEY key, LPCSTR subkey, LPCSTR value);
LWSTDAPI LONG WINAPI SHDeleteKeyW(HKEY key, LPCWSTR subkey);
LWSTDAPI LONG WINAPI SHDeleteKeyA(HKEY key, LPCSTR subkey);
LWSTDAPI LONG WINAPI SHDeleteEmptyKeyW(HKEY key, LPCWSTR subkey);
LWSTDAPI LONG WINAPI SHDeleteEmptyKeyA(HKEY key, LPCSTR subkey);
LWSTDAPI LONG WINAPI SHQueryValueExW(HKEY key, LPCWSTR value, DWORD *reserved, DWORD *type, void *data, DWORD *size);
LWSTDAPI LONG WINAPI SHQueryValueExA(HKEY key, LPCSTR value, DWORD *reserved, DWORD *type, void *data, DWORD *size);
LWSTDAPI LONG WINAPI SHRegGetValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD flags, DWORD *type, void *data,
                                    DWORD *size);
LWSTDAPI LONG WINAPI SHRegGetValueA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD flags, DWORD *type, void *data,
                                    DWORD *size);
LWSTDAPI LONG WINAPI SHRegGetValueFromHKCUHKLM(LPCWSTR subkey, LPCWSTR value, DWORD flags, DWORD *type, void *data,
                                               DWORD *size);
LWSTDAPI BOOL WINAPI SHRegGetBoolValueFromHKCUHKLM(LPCWSTR subkey, LPCWSTR value, BOOL fallback);
LWSTDAPI LONG WINAPI SHRegGetPathW(HKEY key, LPCWSTR subkey, LPCWSTR value, LPWSTR path, DWORD flags);
LWSTDAPI LONG WINAPI SHRegGetPathA(HKEY key, LPCSTR subkey, LPCSTR value, LPSTR path, DWORD flags);
LWSTDAPI LONG WINAPI SHRegSetPathW(HKEY key, LPCWSTR subkey, LPCWSTR value, LPCWSTR path, DWORD flags);
LWSTDAPI LONG WINAPI SHRegSetPathA(HKEY key, LPCSTR subkey, LPCSTR value, LPCSTR path, DWORD flags);
LWSTDAPI LONG WINAPI SHEnumKeyExW(HKEY key, DWORD index, LPWSTR name, DWORD *size);
LWSTDAPI LONG WINAPI SHEnumKeyExA(HKEY key, DWORD index, LPSTR name, DWORD *size);
LWSTDAPI LONG WINAPI SHEnumValueW(HKEY key, DWORD index, LPWSTR name, DWORD *name_size, DWORD *type, void *data,
                                  DWORD *size);
LWSTDAPI LONG WINAPI SHEnumValueA(HKEY key, DWORD index, LPSTR name, DWORD *name_size, DWORD *type, void *data,
                                  DWORD *size);
LWSTDAPI LONG WINAPI SHQueryInfoKeyW(HKEY key, DWORD *subkeys, DWORD *subkey_size, DWORD *values, DWORD *value_size);
LWSTDAPI LONG WINAPI SHQueryInfoKeyA(HKEY key, DWORD *subkeys, DWORD *subkey_size, DWORD *values, DWORD *value_size);
LWSTDAPI LONG WINAPI SHCopyKeyW(HKEY source, LPCWSTR subkey, HKEY destination, DWORD reserved);
LWSTDAPI LONG WINAPI SHCopyKeyA(HKEY source, LPCSTR subkey, HKEY destination, DWORD reserved);
LWSTDAPI HKEY WINAPI SHRegDuplicateHKey(HKEY key);
LWSTDAPI int WINAPI SHRegGetIntW(HKEY key, LPCWSTR value, int fallback);

LWSTDAPI LONG WINAPI SHRegCreateUSKeyW(LPCWSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, DWORD flags);
LWSTDAPI LONG WINAPI SHRegCreateUSKeyA(LPCSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, DWORD flags);
LWSTDAPI LONG WINAPI SHRegOpenUSKeyW(LPCWSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, BOOL ignore_hkcu);
LWSTDAPI LONG WINAPI SHRegOpenUSKeyA(LPCSTR path, REGSAM access, HUSKEY relative, PHUSKEY key, BOOL ignore_hkcu);
LWSTDAPI LONG WINAPI SHRegQueryUSValueW(HUSKEY key, LPCWSTR value, DWORD *type, void *data, DWORD *size,
                                        BOOL ignore_hkcu, void *fallback, DWORD fallback_size);
LWSTDAPI LONG WINAPI SHRegQueryUSValueA(HUSKEY key, LPCSTR value, DWORD *type, void *data, DWORD *size,
                                        BOOL ignore_hkcu, void *fallback, DWORD fallback_size);
LWSTDAPI LONG WINAPI SHRegWriteUSValueW(HUSKEY key, LPCWSTR value, DWORD type, const void *data, DWORD size,
                                        DWORD flags);
LWSTDAPI LONG WINAPI SHRegWriteUSValueA(HUSKEY key, LPCSTR value, DWORD type, const void *data, DWORD size,
                                        DWORD flags);
LWSTDAPI LONG WINAPI SHRegDeleteUSValueW(HUSKEY key, LPCWSTR value, SHREGDEL_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegDeleteUSValueA(HUSKEY key, LPCSTR value, SHREGDEL_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegDeleteEmptyUSKeyW(HUSKEY key, LPCWSTR subkey, SHREGDEL_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegDeleteEmptyUSKeyA(HUSKEY key, LPCSTR subkey, SHREGDEL_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegEnumUSKeyW(HUSKEY key, DWORD index, LPWSTR name, DWORD *size, SHREGENUM_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegEnumUSKeyA(HUSKEY key, DWORD index, LPSTR name, DWORD *size, SHREGENUM_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegEnumUSValueW(HUSKEY key, DWORD index, LPWSTR name, DWORD *name_size, DWORD *type, void *data,
                                       DWORD *size, SHREGENUM_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegEnumUSValueA(HUSKEY key, DWORD index, LPSTR name, DWORD *name_size, DWORD *type, void *data,
                                       DWORD *size, SHREGENUM_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegQueryInfoUSKeyW(HUSKEY key, DWORD *subkeys, DWORD *subkey_size, DWORD *values,
                                          DWORD *value_size, SHREGENUM_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegQueryInfoUSKeyA(HUSKEY key, DWORD *subkeys, DWORD *subkey_size, DWORD *values,
                                          DWORD *value_size, SHREGENUM_FLAGS flags);
LWSTDAPI LONG WINAPI SHRegCloseUSKey(HUSKEY key);
LWSTDAPI LONG WINAPI SHRegGetUSValueW(LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size,
                                      BOOL ignore_hkcu, void *fallback, DWORD fallback_size);
LWSTDAPI LONG WINAPI SHRegGetUSValueA(LPCSTR subkey, LPCSTR value, DWORD *type, void *data, DWORD *size,
                                      BOOL ignore_hkcu, void *fallback, DWORD fallback_size);
LWSTDAPI LONG WINAPI SHRegSetUSValueW(LPCWSTR subkey, LPCWSTR value, DWORD type, const void *data, DWORD size,
                                      DWORD flags);
LWSTDAPI LONG WINAPI SHRegSetUSValueA(LPCSTR subkey, LPCSTR value, DWORD type, const void *data, DWORD size,
                                      DWORD flags);
LWSTDAPI BOOL WINAPI SHRegGetBoolUSValueW(LPCWSTR subkey, LPCWSTR value, BOOL ignore_hkcu, BOOL fallback);
LWSTDAPI BOOL WINAPI SHRegGetBoolUSValueA(LPCSTR subkey, LPCSTR value, BOOL ignore_hkcu, BOOL fallback);

/* ------------------------------------------------------------------ associations */

typedef enum {
    ASSOCF_NONE = 0x00000000,
    ASSOCF_INIT_NOREMAPCLSID = 0x00000001,
    ASSOCF_INIT_BYEXENAME = 0x00000002,
    ASSOCF_OPEN_BYEXENAME = 0x00000002,
    ASSOCF_INIT_DEFAULTTOSTAR = 0x00000004,
    ASSOCF_INIT_DEFAULTTOFOLDER = 0x00000008,
    ASSOCF_NOUSERSETTINGS = 0x00000010,
    ASSOCF_NOTRUNCATE = 0x00000020,
    ASSOCF_VERIFY = 0x00000040,
    ASSOCF_REMAPRUNDLL = 0x00000080,
    ASSOCF_NOFIXUPS = 0x00000100,
    ASSOCF_IGNOREBASECLASS = 0x00000200,
    ASSOCF_INIT_IGNOREUNKNOWN = 0x00000400,
    ASSOCF_INIT_FIXED_PROGID = 0x00000800,
    ASSOCF_IS_PROTOCOL = 0x00001000,
    ASSOCF_INIT_FOR_FILE = 0x00002000,
    ASSOCF_IS_FULL_URI = 0x00004000,
    ASSOCF_PER_MACHINE_ONLY = 0x00008000,
    ASSOCF_APP_TO_APP = 0x00010000,
} ASSOCF;

typedef enum {
    ASSOCSTR_COMMAND = 1,
    ASSOCSTR_EXECUTABLE,
    ASSOCSTR_FRIENDLYDOCNAME,
    ASSOCSTR_FRIENDLYAPPNAME,
    ASSOCSTR_NOOPEN,
    ASSOCSTR_SHELLNEWVALUE,
    ASSOCSTR_DDECOMMAND,
    ASSOCSTR_DDEIFEXEC,
    ASSOCSTR_DDEAPPLICATION,
    ASSOCSTR_DDETOPIC,
    ASSOCSTR_INFOTIP,
    ASSOCSTR_QUICKTIP,
    ASSOCSTR_TILEINFO,
    ASSOCSTR_CONTENTTYPE,
    ASSOCSTR_DEFAULTICON,
    ASSOCSTR_SHELLEXTENSION,
    ASSOCSTR_DROPTARGET,
    ASSOCSTR_DELEGATEEXECUTE,
    ASSOCSTR_SUPPORTED_URI_PROTOCOLS,
    ASSOCSTR_PROGID,
    ASSOCSTR_APPID,
    ASSOCSTR_APPPUBLISHER,
    ASSOCSTR_APPICONREFERENCE,
    ASSOCSTR_MAX,
} ASSOCSTR;

typedef enum {
    ASSOCKEY_SHELLEXECCLASS = 1,
    ASSOCKEY_APP,
    ASSOCKEY_CLASS,
    ASSOCKEY_BASECLASS,
    ASSOCKEY_MAX,
} ASSOCKEY;

typedef enum {
    ASSOCDATA_MSIDESCRIPTOR = 1,
    ASSOCDATA_NOACTIVATEHANDLER,
    ASSOCDATA_UNUSED1,
    ASSOCDATA_HASPERUSERASSOC,
    ASSOCDATA_EDITFLAGS,
    ASSOCDATA_VALUE,
    ASSOCDATA_MAX,
} ASSOCDATA;

typedef enum {
    ASSOCENUM_NONE,
} ASSOCENUM;

typedef enum tagPERCEIVED {
    PERCEIVED_TYPE_FIRST = -3,
    PERCEIVED_TYPE_CUSTOM = -3,
    PERCEIVED_TYPE_UNSPECIFIED = -2,
    PERCEIVED_TYPE_FOLDER = -1,
    PERCEIVED_TYPE_UNKNOWN = 0,
    PERCEIVED_TYPE_TEXT = 1,
    PERCEIVED_TYPE_IMAGE = 2,
    PERCEIVED_TYPE_AUDIO = 3,
    PERCEIVED_TYPE_VIDEO = 4,
    PERCEIVED_TYPE_COMPRESSED = 5,
    PERCEIVED_TYPE_DOCUMENT = 6,
    PERCEIVED_TYPE_SYSTEM = 7,
    PERCEIVED_TYPE_APPLICATION = 8,
    PERCEIVED_TYPE_GAMEMEDIA = 9,
    PERCEIVED_TYPE_CONTACTS = 10,
    PERCEIVED_TYPE_LAST = 10,
} PERCEIVED;

typedef DWORD PERCEIVEDFLAG;
#define PERCEIVEDFLAG_UNDEFINED 0x0000
#define PERCEIVEDFLAG_SOFTCODED 0x0001
#define PERCEIVEDFLAG_HARDCODED 0x0002
#define PERCEIVEDFLAG_NATIVESUPPORT 0x0004
#define PERCEIVEDFLAG_GDIPLUS 0x0010
#define PERCEIVEDFLAG_WMSDK 0x0020
#define PERCEIVEDFLAG_ZIPFOLDER 0x0040

typedef struct IQueryAssociations IQueryAssociations;
typedef struct IQueryAssociationsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IQueryAssociations *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IQueryAssociations *This);
    ULONG(STDMETHODCALLTYPE *Release)(IQueryAssociations *This);
    HRESULT(STDMETHODCALLTYPE *Init)(IQueryAssociations *This, ASSOCF flags, LPCWSTR assoc, HKEY progid, HWND window);
    HRESULT(STDMETHODCALLTYPE *GetString)(IQueryAssociations *This, ASSOCF flags, ASSOCSTR str, LPCWSTR extra,
                                          LPWSTR out, DWORD *size);
    HRESULT(STDMETHODCALLTYPE *GetKey)(IQueryAssociations *This, ASSOCF flags, ASSOCKEY key, LPCWSTR extra,
                                       HKEY *out);
    HRESULT(STDMETHODCALLTYPE *GetData)(IQueryAssociations *This, ASSOCF flags, ASSOCDATA data, LPCWSTR extra,
                                        void *out, DWORD *size);
    HRESULT(STDMETHODCALLTYPE *GetEnum)(IQueryAssociations *This, ASSOCF flags, ASSOCENUM assocenum, LPCWSTR extra,
                                        REFIID riid, void **out);
} IQueryAssociationsVtbl;
struct IQueryAssociations {
    const IQueryAssociationsVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_IQueryAssociations, 0xc46ca590, 0x3c3f, 0x11d2, 0xbe, 0xe6, 0x00, 0x00, 0xf8, 0x05, 0xca, 0x57);
MIME_DEFINE_GUID(CLSID_QueryAssociations, 0xa07034fd, 0x6caa, 0x4954, 0xac, 0x3f, 0x97, 0xa2, 0x72, 0x16, 0xf9, 0x8a);

LWSTDAPI HRESULT WINAPI AssocCreate(CLSID clsid, REFIID riid, void **out);
LWSTDAPI HRESULT WINAPI AssocQueryStringW(ASSOCF flags, ASSOCSTR str, LPCWSTR assoc, LPCWSTR extra, LPWSTR out,
                                          DWORD *size);
LWSTDAPI HRESULT WINAPI AssocQueryStringA(ASSOCF flags, ASSOCSTR str, LPCSTR assoc, LPCSTR extra, LPSTR out,
                                          DWORD *size);
LWSTDAPI HRESULT WINAPI AssocQueryStringByKeyW(ASSOCF flags, ASSOCSTR str, HKEY key, LPCWSTR extra, LPWSTR out,
                                               DWORD *size);
LWSTDAPI HRESULT WINAPI AssocQueryStringByKeyA(ASSOCF flags, ASSOCSTR str, HKEY key, LPCSTR extra, LPSTR out,
                                               DWORD *size);
LWSTDAPI HRESULT WINAPI AssocQueryKeyW(ASSOCF flags, ASSOCKEY key, LPCWSTR assoc, LPCWSTR extra, HKEY *out);
LWSTDAPI HRESULT WINAPI AssocQueryKeyA(ASSOCF flags, ASSOCKEY key, LPCSTR assoc, LPCSTR extra, HKEY *out);
LWSTDAPI BOOL WINAPI AssocIsDangerous(LPCWSTR assoc);
LWSTDAPI HRESULT WINAPI AssocGetPerceivedType(LPCWSTR extension, PERCEIVED *type, PERCEIVEDFLAG *flags,
                                              LPWSTR *name);

/* ------------------------------------------------------------------ com helpers */

typedef struct {
    const IID *piid;
    DWORD dwOffset;
} QITAB, *LPQITAB;
typedef const QITAB *LPCQITAB;

#define QITABENTMULTI(Cthis, Ifoo, Iimpl) {&IID_##Ifoo, (DWORD)((ULONG_PTR)(Iimpl *)(Cthis *)8 - 8)}
#define QITABENT(Cthis, Ifoo) QITABENTMULTI(Cthis, Ifoo, Ifoo)

LWSTDAPI HRESULT WINAPI QISearch(void *base, LPCQITAB table, REFIID riid, void **out);
LWSTDAPI void WINAPI IUnknown_AtomicRelease(void **unknown);
LWSTDAPI void WINAPI IUnknown_Set(IUnknown **slot, IUnknown *unknown);
LWSTDAPI HRESULT WINAPI IUnknown_GetSite(IUnknown *unknown, REFIID riid, void **site);
LWSTDAPI HRESULT WINAPI IUnknown_SetSite(IUnknown *unknown, IUnknown *site);
LWSTDAPI HRESULT WINAPI IUnknown_QueryService(IUnknown *unknown, REFGUID service, REFIID riid, void **out);
LWSTDAPI HRESULT WINAPI IUnknown_GetWindow(IUnknown *unknown, HWND *window);
LWSTDAPI HRESULT WINAPI ConnectToConnectionPoint(IUnknown *sink, REFIID riid, BOOL connect, IUnknown *source,
                                                 DWORD *cookie, void **point);
LWSTDAPI HRESULT WINAPI IStream_Read(IStream *stream, void *data, ULONG size);
LWSTDAPI HRESULT WINAPI IStream_Write(IStream *stream, const void *data, ULONG size);
LWSTDAPI HRESULT WINAPI IStream_Reset(IStream *stream);
LWSTDAPI HRESULT WINAPI IStream_Size(IStream *stream, ULARGE_INTEGER *size);
LWSTDAPI HRESULT WINAPI IStream_Copy(IStream *from, IStream *to, DWORD size);
LWSTDAPI HRESULT WINAPI IStream_ReadStr(IStream *stream, LPWSTR *string);
LWSTDAPI HRESULT WINAPI IStream_WriteStr(IStream *stream, LPCWSTR string);
LWSTDAPI IStream *WINAPI SHCreateMemStream(const BYTE *data, UINT size);
LWSTDAPI HRESULT WINAPI SHCreateStreamOnFileW(LPCWSTR file, DWORD mode, IStream **stream);
LWSTDAPI HRESULT WINAPI SHCreateStreamOnFileA(LPCSTR file, DWORD mode, IStream **stream);
LWSTDAPI HRESULT WINAPI SHCreateStreamOnFileEx(LPCWSTR file, DWORD mode, DWORD attributes, BOOL create,
                                               IStream *template_stream, IStream **stream);

/* ------------------------------------------------------------------ everything else */

typedef struct _DLLVERSIONINFO {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformID;
} DLLVERSIONINFO;

typedef struct _DLLVERSIONINFO2 {
    DLLVERSIONINFO info1;
    DWORD dwFlags;
    ULONGLONG ullVersion;
} DLLVERSIONINFO2;

#define DLLVER_PLATFORM_WINDOWS 0x00000001
#define DLLVER_PLATFORM_NT 0x00000002

#define SHACF_DEFAULT 0x00000000
#define SHACF_FILESYSTEM 0x00000001
#define SHACF_URLALL (SHACF_URLHISTORY | SHACF_URLMRU)
#define SHACF_URLHISTORY 0x00000002
#define SHACF_URLMRU 0x00000004
#define SHACF_USETAB 0x00000008
#define SHACF_FILESYS_ONLY 0x00000010
#define SHACF_FILESYS_DIRS 0x00000020
#define SHACF_VIRTUAL_NAMESPACE 0x00000040
#define SHACF_AUTOSUGGEST_FORCE_ON 0x10000000
#define SHACF_AUTOSUGGEST_FORCE_OFF 0x20000000
#define SHACF_AUTOAPPEND_FORCE_ON 0x40000000
#define SHACF_AUTOAPPEND_FORCE_OFF 0x80000000

LWSTDAPI HRESULT WINAPI DllGetVersion(DLLVERSIONINFO *info);
LWSTDAPI COLORREF WINAPI ColorHLSToRGB(WORD hue, WORD luminance, WORD saturation);
LWSTDAPI void WINAPI ColorRGBToHLS(COLORREF color, WORD *hue, WORD *luminance, WORD *saturation);
LWSTDAPI COLORREF WINAPI ColorAdjustLuma(COLORREF color, int luma, BOOL scale);
LWSTDAPI int WINAPI GetMenuPosFromID(HMENU menu, UINT id);
LWSTDAPI HRESULT WINAPI SHAutoComplete(HWND edit, DWORD flags);
LWSTDAPI BOOL WINAPI IsOS(DWORD os);
LWSTDAPI HANDLE WINAPI SHAllocShared(const void *data, DWORD size, DWORD process);
LWSTDAPI void *WINAPI SHLockShared(HANDLE shared, DWORD process);
LWSTDAPI BOOL WINAPI SHUnlockShared(void *view);
LWSTDAPI BOOL WINAPI SHFreeShared(HANDLE shared, DWORD process);
LWSTDAPI HWND WINAPI SHCreateWorkerWindowW(WNDPROC proc, HWND parent, DWORD ex_style, DWORD style, HMENU menu,
                                           LONG_PTR extra);
LWSTDAPI UINT WINAPI WhichPlatform(void);
LWSTDAPI BOOL WINAPI SHIsLowMemoryMachine(DWORD type);
LWSTDAPI HRESULT WINAPI SHGetViewStatePropertyBag(const struct _ITEMIDLIST *pidl, LPCWSTR bag, DWORD flags,
                                                  REFIID riid, void **out);
LWSTDAPI HPALETTE WINAPI SHCreateShellPalette(HDC dc);
LWSTDAPI HRESULT WINAPI SHGetInverseCMAP(BYTE *map, ULONG size);
LWSTDAPI HINSTANCE WINAPI MLLoadLibraryW(LPCWSTR name, HMODULE parent, DWORD flags);
LWSTDAPI HINSTANCE WINAPI MLLoadLibraryA(LPCSTR name, HMODULE parent, DWORD flags);
LWSTDAPI DWORD WINAPI SHSendMessageBroadcastW(UINT message, WPARAM wparam, LPARAM lparam);
LWSTDAPI DWORD WINAPI SHSendMessageBroadcastA(UINT message, WPARAM wparam, LPARAM lparam);
LWSTDAPI int WINAPI SHMessageBoxCheckW(HWND parent, LPCWSTR text, LPCWSTR caption, UINT type, int fallback,
                                       LPCWSTR name);
LWSTDAPI int WINAPI SHMessageBoxCheckA(HWND parent, LPCSTR text, LPCSTR caption, UINT type, int fallback,
                                       LPCSTR name);
LWSTDAPI WCHAR WINAPI SHStripMneumonicW(LPWSTR text);
LWSTDAPI CHAR WINAPI SHStripMneumonicA(LPSTR text);
LWSTDAPI BOOL WINAPI SHSkipJunction(IBindCtx *context, const CLSID *clsid);

#endif
