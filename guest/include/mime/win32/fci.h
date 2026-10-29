/* cabinet.dll's cabinet writer: the callbacks the caller supplies for files and memory, and the cabinet description. */
#ifndef MIME_WIN32_FCI_H
#define MIME_WIN32_FCI_H

#include <mime/win32/fdi.h>

#ifdef MIME_BUILD_CABINET
#define FCIAPI DECLSPEC_EXPORT
#else
#define FCIAPI DECLSPEC_IMPORT
#endif

#define CB_MAX_DISK_NAME 256
#define CB_MAX_CABINET_NAME 256
#define CB_MAX_CAB_PATH 256

typedef enum {
    FCIERR_NONE,
    FCIERR_OPEN_SRC,
    FCIERR_READ_SRC,
    FCIERR_ALLOC_FAIL,
    FCIERR_TEMP_FILE,
    FCIERR_BAD_COMPR_TYPE,
    FCIERR_CAB_FILE,
    FCIERR_USER_ABORT,
    FCIERR_MCI_FAIL,
    FCIERR_CAB_FORMAT_LIMIT,
} FCIERROR;

typedef USHORT TCOMP;
#define tcompMASK_TYPE 0x000F
#define tcompTYPE_NONE 0x0000
#define tcompTYPE_MSZIP 0x0001
#define tcompTYPE_QUANTUM 0x0002
#define tcompTYPE_LZX 0x0003

#define statusFile 0
#define statusFolder 1
#define statusCabinet 2

typedef struct {
    ULONG cb;
    ULONG cbFolderThresh;
    UINT cbReserveCFHeader;
    UINT cbReserveCFFolder;
    UINT cbReserveCFData;
    int iCab;
    int iDisk;
    int fFailOnIncompressible;
    USHORT setID;
    char szDisk[CB_MAX_DISK_NAME];
    char szCab[CB_MAX_CABINET_NAME];
    char szCabPath[CB_MAX_CAB_PATH];
} CCAB, *PCCAB;

typedef void *HFCI;

typedef void *(DIAMONDAPI *PFNFCIALLOC)(ULONG cb);
typedef void(DIAMONDAPI *PFNFCIFREE)(void *memory);
typedef INT_PTR(DIAMONDAPI *PFNFCIOPEN)(char *file, int oflag, int pmode, int *err, void *pv);
typedef UINT(DIAMONDAPI *PFNFCIREAD)(INT_PTR file, void *memory, UINT cb, int *err, void *pv);
typedef UINT(DIAMONDAPI *PFNFCIWRITE)(INT_PTR file, void *memory, UINT cb, int *err, void *pv);
typedef int(DIAMONDAPI *PFNFCICLOSE)(INT_PTR file, int *err, void *pv);
typedef long(DIAMONDAPI *PFNFCISEEK)(INT_PTR file, long dist, int seektype, int *err, void *pv);
typedef int(DIAMONDAPI *PFNFCIDELETE)(char *file, int *err, void *pv);
typedef BOOL(DIAMONDAPI *PFNFCIGETTEMPFILE)(char *name, int cb, void *pv);
typedef int(DIAMONDAPI *PFNFCIFILEPLACED)(PCCAB cab, char *file, long cb, BOOL continuation, void *pv);
typedef BOOL(DIAMONDAPI *PFNFCIGETNEXTCABINET)(PCCAB cab, ULONG cb_prev, void *pv);
typedef long(DIAMONDAPI *PFNFCISTATUS)(UINT type, ULONG cb1, ULONG cb2, void *pv);
typedef INT_PTR(DIAMONDAPI *PFNFCIGETOPENINFO)(char *name, USHORT *date, USHORT *time, USHORT *attribs, int *err, void *pv);

FCIAPI HFCI DIAMONDAPI FCICreate(PERF perf, PFNFCIFILEPLACED placed, PFNFCIALLOC alloc, PFNFCIFREE free_memory,
                                 PFNFCIOPEN open, PFNFCIREAD read, PFNFCIWRITE write, PFNFCICLOSE close,
                                 PFNFCISEEK seek, PFNFCIDELETE remove_file, PFNFCIGETTEMPFILE temp_file, PCCAB cab,
                                 void *pv);
FCIAPI BOOL DIAMONDAPI FCIAddFile(HFCI fci, char *source, char *name, BOOL execute, PFNFCIGETNEXTCABINET next_cabinet,
                                  PFNFCISTATUS status, PFNFCIGETOPENINFO open_info, TCOMP compression);
FCIAPI BOOL DIAMONDAPI FCIFlushFolder(HFCI fci, PFNFCIGETNEXTCABINET next_cabinet, PFNFCISTATUS status);
FCIAPI BOOL DIAMONDAPI FCIFlushCabinet(HFCI fci, BOOL get_next, PFNFCIGETNEXTCABINET next_cabinet,
                                       PFNFCISTATUS status);
FCIAPI BOOL DIAMONDAPI FCIDestroy(HFCI fci);

#endif
