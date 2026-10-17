/* what the parts of msacm32 share. */
#ifndef MSACM32_PRIVATE_H
#define MSACM32_PRIVATE_H

#define MIME_BUILD_MSACM32
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/msacmdrv.h>

/* the kinds of handles, the first word of each. */
#define ACM_ANY 0
#define ACM_ID 1
#define ACM_DRIVER 2
#define ACM_STREAM 3

/* the state of a driver id: windows keeps the disabled, local and notify bits where fdwSupport has them. */
#define ID_LOADED 0x00000001
#define ID_CONFIGURABLE 0x00000002
#define ID_ABOUT 0x00000004
#define ID_NOTIFY 0x10000000
#define ID_LOCAL 0x40000000
#define ID_DISABLED 0x80000000

/* the internal enumeration flags of windows: notification drivers, and drivers that failed to load. */
#define ENUM_NOTIFY 0x10000000
#define ENUM_BROKEN 0x20000000
#define ENUM_ALL 0xFFFFFFFF

typedef struct ACMTAG {
    DWORD tag;
    DWORD size;
} ACMTAG;

typedef struct ACMDRIVER_ ACMDRIVER_;
typedef struct ACMSTREAM_ ACMSTREAM_;

typedef struct ACMDRIVERID_ {
    DWORD type;
    struct ACMDRIVERID_ *next;
    DWORD thread;          /* the thread of a local driver, 0 for a global one */
    DWORD add;             /* the flags it was added with */
    LPARAM param;          /* the function or window it was added with */
    DWORD state;
    BOOL broken;           /* failed to load */
    DWORD priority;
    DWORD support;
    DWORD format_count, filter_count;
    ACMTAG *formats, *filters;
    DRIVERPROC proc;       /* a function driver, or -1 for a notification window */
    DWORD_PTR id;          /* what a function driver's open without a description gave */
    HDRVR hdrvr;           /* an installable driver, open without a description while loaded */
    ACMDRIVER_ *drivers;
    WCHAR alias[144];
    RTL_CRITICAL_SECTION lock;
} ACMDRIVERID_;

struct ACMDRIVER_ {
    DWORD type;
    ACMDRIVER_ *next;
    ACMDRIVERID_ *id;
    HDRVR hdrvr;
    DRIVERPROC proc;
    DWORD_PTR instance;
    ACMSTREAM_ *streams;
    DWORD thread;
    RTL_CRITICAL_SECTION lock;
};

struct ACMSTREAM_ {
    DWORD type;
    ACMSTREAM_ *next;
    ACMDRIVER_ *driver;
    BOOL given;            /* the driver was the caller's, else the stream closes it */
    DWORD prepared;
    ACMDRVSTREAMINSTANCE instance;
    RTL_CRITICAL_SECTION lock;
    BYTE formats[];
};

extern HINSTANCE AcmModule;
extern RTL_CRITICAL_SECTION AcmLock;

/* main.c */
BOOL AcmBoot(void);
BOOL AcmValid(const void *handle, DWORD type);
DWORD AcmFormatSize(const WAVEFORMATEX *format);
BOOL AcmReadable(const void *p, SIZE_T size);
BOOL AcmWritable(void *p, SIZE_T size);
BOOL AcmReadableFormat(const WAVEFORMATEX *format);
BOOL AcmReadableFilter(const WAVEFILTER *filter);
void AcmCopyString(WCHAR *to, const WCHAR *from, SIZE_T count);
void AcmToAnsi(char *to, const WCHAR *from, int count);
void AcmToWide(WCHAR *to, const char *from, int count);
BOOL AcmEnumerating(void);
void AcmEnterEnum(void);
void AcmLeaveEnum(void);

/* driver.c */
ACMDRIVERID_ *AcmNextId(ACMDRIVERID_ *after, DWORD flags);
LRESULT AcmIdMessage(ACMDRIVERID_ *id, UINT message, LPARAM param1, LPARAM param2);
LRESULT AcmDriverMessage(ACMDRIVER_ *driver, UINT message, LPARAM param1, LPARAM param2);
MMRESULT AcmOpenDriver(ACMDRIVER_ **out, ACMDRIVERID_ *id);
MMRESULT AcmCloseDriver(ACMDRIVER_ *driver);
DWORD AcmIdSupport(const ACMDRIVERID_ *id);
BOOL AcmHasFormatTag(const ACMDRIVERID_ *id, DWORD tag);
BOOL AcmHasFilterTag(const ACMDRIVERID_ *id, DWORD tag);
ACMDRIVERID_ *AcmIdOf(HACMOBJ handle);

/* format.c */
MMRESULT AcmMaxFormatSize(HACMOBJ handle, DWORD *size);
MMRESULT AcmMaxFilterSize(HACMOBJ handle, DWORD *size);
MMRESULT AcmFormatTagDetails(ACMDRIVERID_ *id, ACMFORMATTAGDETAILSW *details, DWORD flags);
void AcmFormatName(ACMFORMATDETAILSW *details);
void AcmPcmTagName(WCHAR *name);

/* pcm.c */
LRESULT CALLBACK AcmPcmDriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2);

#endif
