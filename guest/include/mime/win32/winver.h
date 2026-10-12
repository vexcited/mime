/*
 * file version resources, the api of version.dll.
 */
#ifndef MIME_WIN32_WINVER_H
#define MIME_WIN32_WINVER_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_VERSION
#define VERAPI __declspec(dllexport)
#else
#define VERAPI DECLSPEC_IMPORT
#endif

#define VS_FILE_INFO RT_VERSION
#define VS_VERSION_INFO 1
#define VS_USER_DEFINED 100

#define VS_FFI_SIGNATURE 0xFEEF04BDL
#define VS_FFI_STRUCVERSION 0x00010000L
#define VS_FFI_FILEFLAGSMASK 0x0000003FL

#define VS_FF_DEBUG 0x00000001L
#define VS_FF_PRERELEASE 0x00000002L
#define VS_FF_PATCHED 0x00000004L
#define VS_FF_PRIVATEBUILD 0x00000008L
#define VS_FF_INFOINFERRED 0x00000010L
#define VS_FF_SPECIALBUILD 0x00000020L

#define VOS_UNKNOWN 0x00000000L
#define VOS_DOS 0x00010000L
#define VOS_NT 0x00040000L
#define VOS__WINDOWS32 0x00000004L
#define VOS_NT_WINDOWS32 0x00040004L

#define VFT_UNKNOWN 0x00000000L
#define VFT_APP 0x00000001L
#define VFT_DLL 0x00000002L
#define VFT_DRV 0x00000003L
#define VFT_FONT 0x00000004L
#define VFT_VXD 0x00000005L
#define VFT_STATIC_LIB 0x00000007L

/* GetFileVersionInfoEx flags. */
#define FILE_VER_GET_LOCALISED 0x01
#define FILE_VER_GET_NEUTRAL 0x02
#define FILE_VER_GET_PREFETCHED 0x04

/* VerFindFile and VerInstallFile results. */
#define VFFF_ISSHAREDFILE 0x0001
#define VFF_CURNEDEST 0x0001
#define VFF_FILEINUSE 0x0002
#define VFF_BUFFTOOSMALL 0x0004
#define VIFF_FORCEINSTALL 0x0001
#define VIFF_DONTDELETEOLD 0x0002
#define VIF_TEMPFILE 0x00000001L
#define VIF_MISMATCH 0x00000002L
#define VIF_SRCOLD 0x00000004L
#define VIF_DIFFLANG 0x00000008L
#define VIF_DIFFCODEPG 0x00000010L
#define VIF_DIFFTYPE 0x00000020L
#define VIF_WRITEPROT 0x00000040L
#define VIF_FILEINUSE 0x00000080L
#define VIF_OUTOFSPACE 0x00000100L
#define VIF_ACCESSVIOLATION 0x00000200L
#define VIF_SHARINGVIOLATION 0x00000400L
#define VIF_CANNOTCREATE 0x00000800L
#define VIF_CANNOTDELETE 0x00001000L
#define VIF_CANNOTRENAME 0x00002000L
#define VIF_CANNOTDELETECUR 0x00004000L
#define VIF_OUTOFMEMORY 0x00008000L
#define VIF_CANNOTREADSRC 0x00010000L
#define VIF_CANNOTREADDST 0x00020000L
#define VIF_BUFFTOOSMALL 0x00040000L
#define VIF_CANNOTLOADLZ32 0x00080000L
#define VIF_CANNOTLOADCABINET 0x00100000L

typedef struct tagVS_FIXEDFILEINFO {
    DWORD dwSignature;
    DWORD dwStrucVersion;
    DWORD dwFileVersionMS;
    DWORD dwFileVersionLS;
    DWORD dwProductVersionMS;
    DWORD dwProductVersionLS;
    DWORD dwFileFlagsMask;
    DWORD dwFileFlags;
    DWORD dwFileOS;
    DWORD dwFileType;
    DWORD dwFileSubtype;
    DWORD dwFileDateMS;
    DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;

VERAPI DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR file, LPDWORD handle);
VERAPI DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR file, LPDWORD handle);
VERAPI DWORD WINAPI GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR file, LPDWORD handle);
VERAPI DWORD WINAPI GetFileVersionInfoSizeExA(DWORD flags, LPCSTR file, LPDWORD handle);
VERAPI BOOL WINAPI GetFileVersionInfoW(LPCWSTR file, DWORD handle, DWORD size, LPVOID data);
VERAPI BOOL WINAPI GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD size, LPVOID data);
VERAPI BOOL WINAPI GetFileVersionInfoExW(DWORD flags, LPCWSTR file, DWORD handle, DWORD size, LPVOID data);
VERAPI BOOL WINAPI GetFileVersionInfoExA(DWORD flags, LPCSTR file, DWORD handle, DWORD size, LPVOID data);
VERAPI BOOL WINAPI VerQueryValueW(LPCVOID block, LPCWSTR sub_block, LPVOID *buffer, PUINT length);
VERAPI BOOL WINAPI VerQueryValueA(LPCVOID block, LPCSTR sub_block, LPVOID *buffer, PUINT length);
VERAPI DWORD WINAPI VerLanguageNameW(DWORD lang, LPWSTR buffer, DWORD size);
VERAPI DWORD WINAPI VerLanguageNameA(DWORD lang, LPSTR buffer, DWORD size);
VERAPI DWORD WINAPI VerFindFileW(DWORD flags, LPCWSTR file, LPCWSTR win_dir, LPCWSTR app_dir, LPWSTR cur_dir,
                                 PUINT cur_dir_len, LPWSTR dest_dir, PUINT dest_dir_len);
VERAPI DWORD WINAPI VerFindFileA(DWORD flags, LPCSTR file, LPCSTR win_dir, LPCSTR app_dir, LPSTR cur_dir,
                                 PUINT cur_dir_len, LPSTR dest_dir, PUINT dest_dir_len);
VERAPI DWORD WINAPI VerInstallFileW(DWORD flags, LPCWSTR src_file, LPCWSTR dest_file, LPCWSTR src_dir,
                                    LPCWSTR dest_dir, LPCWSTR cur_dir, LPWSTR tmp_file, PUINT tmp_file_len);
VERAPI DWORD WINAPI VerInstallFileA(DWORD flags, LPCSTR src_file, LPCSTR dest_file, LPCSTR src_dir,
                                    LPCSTR dest_dir, LPCSTR cur_dir, LPSTR tmp_file, PUINT tmp_file_len);

#endif
