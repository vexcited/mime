/*
 * the cabinet reader of cabinet.dll: a callback for each step of the file system and for each file, and the structures
 * the callbacks share with the caller.
 */
#ifndef MIME_WIN32_FDI_H
#define MIME_WIN32_FDI_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_CABINET
#define FDIAPI DECLSPEC_EXPORT
#else
#define FDIAPI DECLSPEC_IMPORT
#endif

#define DIAMONDAPI __cdecl

typedef struct {
    int erfOper;
    int erfType;
    BOOL fError;
} ERF, *PERF;

typedef enum {
    FDIERROR_NONE,
    FDIERROR_CABINET_NOT_FOUND,
    FDIERROR_NOT_A_CABINET,
    FDIERROR_UNKNOWN_CABINET_VERSION,
    FDIERROR_CORRUPT_CABINET,
    FDIERROR_ALLOC_FAIL,
    FDIERROR_BAD_COMPR_TYPE,
    FDIERROR_MDI_FAIL,
    FDIERROR_TARGET_FILE,
    FDIERROR_RESERVE_MISMATCH,
    FDIERROR_WRONG_CABINET,
    FDIERROR_USER_ABORT,
    FDIERROR_EOF,
} FDIERROR;

#define cpuUNKNOWN (-1)
#define cpu80286 0
#define cpu80386 1

typedef void *HFDI;

typedef struct {
    long cbCabinet;
    USHORT cFolders;
    USHORT cFiles;
    USHORT setID;
    USHORT iCabinet;
    BOOL fReserve;
    BOOL hasprev;
    BOOL hasnext;
} FDICABINETINFO, *PFDICABINETINFO;

typedef enum {
    fdintCABINET_INFO,
    fdintPARTIAL_FILE,
    fdintCOPY_FILE,
    fdintCLOSE_FILE_INFO,
    fdintNEXT_CABINET,
    fdintENUMERATE,
} FDINOTIFICATIONTYPE;

typedef struct {
    long cb;
    char *psz1;
    char *psz2;
    char *psz3;
    void *pv;
    INT_PTR hf;
    USHORT date;
    USHORT time;
    USHORT attribs;
    USHORT setID;
    USHORT iCabinet;
    USHORT iFolder;
    FDIERROR fdie;
} FDINOTIFICATION, *PFDINOTIFICATION;

typedef enum {
    fdidtNEW_CABINET,
    fdidtNEW_FOLDER,
    fdidtDECRYPT,
} FDIDECRYPTTYPE;

typedef struct {
    FDIDECRYPTTYPE fdidt;
    void *pvUser;
    union {
        struct {
            void *pHeaderReserve;
            USHORT cbHeaderReserve;
            USHORT setID;
            int iCabinet;
        } cabinet;
        struct {
            void *pDataReserve;
            USHORT cbDataReserve;
        } folder;
        struct {
            void *pDataReserve;
            USHORT cbDataReserve;
            void *pbData;
            USHORT cbData;
            BOOL fSplit;
            USHORT cbPartial;
        } decrypt;
    };
} FDIDECRYPT, *PFDIDECRYPT;

#define _A_EXEC 0x40
#define _A_NAME_IS_UTF 0x80

typedef void *(DIAMONDAPI *PFNALLOC)(ULONG cb);
typedef void(DIAMONDAPI *PFNFREE)(void *pv);
typedef INT_PTR(DIAMONDAPI *PFNOPEN)(char *pszFile, int oflag, int pmode);
typedef UINT(DIAMONDAPI *PFNREAD)(INT_PTR hf, void *pv, UINT cb);
typedef UINT(DIAMONDAPI *PFNWRITE)(INT_PTR hf, void *pv, UINT cb);
typedef int(DIAMONDAPI *PFNCLOSE)(INT_PTR hf);
typedef long(DIAMONDAPI *PFNSEEK)(INT_PTR hf, long dist, int seektype);
typedef INT_PTR(DIAMONDAPI *PFNFDINOTIFY)(FDINOTIFICATIONTYPE fdint, PFDINOTIFICATION pfdin);
typedef int(DIAMONDAPI *PFNFDIDECRYPT)(PFDIDECRYPT pfdid);

FDIAPI HFDI DIAMONDAPI FDICreate(PFNALLOC pfnalloc, PFNFREE pfnfree, PFNOPEN pfnopen, PFNREAD pfnread,
                                 PFNWRITE pfnwrite, PFNCLOSE pfnclose, PFNSEEK pfnseek, int cpuType, PERF perf);
FDIAPI BOOL DIAMONDAPI FDIIsCabinet(HFDI hfdi, INT_PTR hf, PFDICABINETINFO pfdici);
FDIAPI BOOL DIAMONDAPI FDICopy(HFDI hfdi, char *pszCabinet, char *pszCabPath, int flags, PFNFDINOTIFY pfnfdin,
                               PFNFDIDECRYPT pfnfdid, void *pvUser);
FDIAPI BOOL DIAMONDAPI FDIDestroy(HFDI hfdi);
FDIAPI BOOL DIAMONDAPI FDITruncateCabinet(HFDI hfdi, char *pszCabinetName, USHORT iFolderToDelete);

#endif
