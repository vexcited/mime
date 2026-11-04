#ifndef MIME_MSCMS_PRIVATE_H
#define MIME_MSCMS_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>
#include <rtl.h>
#include <icc/icc.h>

#define PROFILE_FILENAME 1
#define PROFILE_MEMBUFFER 2
#define PROFILE_READ 1
#define PROFILE_READWRITE 2

#define ERROR_INVALID_CMM 2010
#define ERROR_INVALID_PROFILE 2011
#define ERROR_TAG_NOT_FOUND 2012
#define ERROR_TAG_NOT_PRESENT 2013
#define ERROR_DUPLICATE_TAG 2014
#define ERROR_PROFILE_NOT_ASSOCIATED_WITH_DEVICE 2015
#define ERROR_PROFILE_NOT_FOUND 2016
#define ERROR_INVALID_COLORSPACE 2017
#define ERROR_INVALID_TRANSFORM 2020
#define ERROR_COLORSPACE_MISMATCH 2021
#define ERROR_INVALID_COLORINDEX 2022

/* windows hands out its objects as pointers mixed with 'ICM ', so a handle is never the pointer itself. */
#define HANDLE_KEY ((ULONG_PTR)0x49434D20)
#define PROFILE_MAGIC 0x50524F4C
#define TRANSFORM_MAGIC 0x5846524D

typedef struct PROFILE {
    DWORD dwType;
    PVOID pProfileData;
    DWORD cbDataSize;
} PROFILE;

typedef struct PROFILEHEADER {
    DWORD phSize, phCMMType, phVersion, phClass, phDataColorSpace, phConnectionSpace;
    DWORD phDateTime[3];
    DWORD phSignature, phPlatform, phProfileFlags, phManufacturer, phModel;
    DWORD phAttributes[2];
    DWORD phRenderingIntent;
    LONG phIlluminant[3];
    DWORD phCreator;
    BYTE phReserved[44];
} PROFILEHEADER;

typedef struct Profile {
    DWORD magic;
    CRITICAL_SECTION lock;
    IccEdit edit;
    DWORD access;
    BOOL dirty;
    /* the file a profile opened for writing goes back to when it is closed. */
    WCHAR *path;
    /* opened with WcsOpenColorProfile, which WcsCreateIccProfile takes. */
    BOOL wcs;
} Profile;

typedef struct Transform {
    DWORD magic;
    IccXform *xform;
    CRITICAL_SECTION lock;
} Transform;

typedef struct LOGCOLORSPACEA {
    DWORD lcsSignature, lcsVersion, lcsSize;
    LONG lcsCSType, lcsIntent;
    CIEXYZTRIPLE lcsEndpoints;
    DWORD lcsGammaRed, lcsGammaGreen, lcsGammaBlue;
    char lcsFilename[MAX_PATH];
} LOGCOLORSPACEA;

extern const MimeAllocator mscms_alloc;

HANDLE WINAPI OpenColorProfileW(PROFILE *description, DWORD access, DWORD share, DWORD creation);
BOOL WINAPI CloseColorProfile(HANDLE handle);
BOOL WINAPI CreateProfileFromLogColorSpaceW(LOGCOLORSPACEW *lcs, BYTE **profile);

Profile *ProfileOf(HANDLE handle);
HANDLE ProfileHandle(Profile *p);
/* a profile from bytes, copied, or NULL with the last error set. */
HANDLE ProfileFromBytes(const BYTE *data, DWORD size, DWORD access);
Transform *TransformOf(HANDLE handle);

/* the color directory with no slash at the end, and a file name in it when the name is not a path. */
BOOL ColorDirectory(WCHAR *path, DWORD count);
BOOL ColorPath(const WCHAR *name, WCHAR *path, DWORD count);
WCHAR *WideFromAnsi(const char *s);
/* a wide string as ansi into a buffer of bytes: needed counts the null. */
BOOL AnsiOut(const WCHAR *s, char *buffer, DWORD *size);

#endif
