/* acm driver messages and the structures they carry. */
#ifndef MIME_WIN32_MSACMDRV_H
#define MIME_WIN32_MSACMDRV_H

#include <mime/win32/msacm.h>

#define ACMDM_DRIVER_NOTIFY (ACMDM_BASE + 1)
#define ACMDM_DRIVER_DETAILS (ACMDM_BASE + 10)
#define ACMDM_HARDWARE_WAVE_CAPS_INPUT (ACMDM_BASE + 20)
#define ACMDM_HARDWARE_WAVE_CAPS_OUTPUT (ACMDM_BASE + 21)
#define ACMDM_FORMATTAG_DETAILS (ACMDM_BASE + 25)
#define ACMDM_FORMAT_DETAILS (ACMDM_BASE + 26)
#define ACMDM_FORMAT_SUGGEST (ACMDM_BASE + 27)
#define ACMDM_FILTERTAG_DETAILS (ACMDM_BASE + 50)
#define ACMDM_FILTER_DETAILS (ACMDM_BASE + 51)
#define ACMDM_STREAM_OPEN (ACMDM_BASE + 76)
#define ACMDM_STREAM_CLOSE (ACMDM_BASE + 77)
#define ACMDM_STREAM_SIZE (ACMDM_BASE + 78)
#define ACMDM_STREAM_CONVERT (ACMDM_BASE + 79)
#define ACMDM_STREAM_RESET (ACMDM_BASE + 80)
#define ACMDM_STREAM_PREPARE (ACMDM_BASE + 81)
#define ACMDM_STREAM_UNPREPARE (ACMDM_BASE + 82)
#define ACMDM_STREAM_UPDATE (ACMDM_BASE + 83)

#define MAKE_ACM_VERSION(major, minor, build) (((LONG)(major) << 24) | ((LONG)(minor) << 16) | ((LONG)(build)))

#pragma pack(push, 1)

typedef struct {
    DWORD cbStruct;
    FOURCC fccType;
    FOURCC fccComp;
    DWORD dwVersion;
    DWORD dwFlags;
    DWORD dwError;
    LPCWSTR pszSectionName;
    LPCWSTR pszAliasName;
    DWORD dnDevNode;
} ACMDRVOPENDESCW, *PACMDRVOPENDESCW, *LPACMDRVOPENDESCW;

typedef struct {
    DWORD cbStruct;
    LPWAVEFORMATEX pwfxSrc;
    LPWAVEFORMATEX pwfxDst;
    LPWAVEFILTER pwfltr;
    DWORD_PTR dwCallback;
    DWORD_PTR dwInstance;
    DWORD fdwOpen;
    DWORD fdwDriver;
    DWORD_PTR dwDriver;
    HACMSTREAM has;
} ACMDRVSTREAMINSTANCE, *PACMDRVSTREAMINSTANCE, *LPACMDRVSTREAMINSTANCE;

/* an ACMSTREAMHEADER whose reserved words the acm and the driver share. */
typedef struct tACMDRVSTREAMHEADER ACMDRVSTREAMHEADER, *PACMDRVSTREAMHEADER, *LPACMDRVSTREAMHEADER;
struct tACMDRVSTREAMHEADER {
    DWORD cbStruct;
    DWORD fdwStatus;
    DWORD_PTR dwUser;
    LPBYTE pbSrc;
    DWORD cbSrcLength;
    DWORD cbSrcLengthUsed;
    DWORD_PTR dwSrcUser;
    LPBYTE pbDst;
    DWORD cbDstLength;
    DWORD cbDstLengthUsed;
    DWORD_PTR dwDstUser;
    DWORD fdwConvert;
    LPACMDRVSTREAMHEADER padshNext;
    DWORD fdwDriver;
    DWORD_PTR dwDriver;
    DWORD fdwPrepared;
    DWORD_PTR dwPrepared;
    LPBYTE pbPreparedSrc;
    DWORD cbPreparedSrcLength;
    LPBYTE pbPreparedDst;
    DWORD cbPreparedDstLength;
};

typedef struct {
    DWORD cbStruct;
    DWORD fdwSize;
    DWORD cbSrcLength;
    DWORD cbDstLength;
} ACMDRVSTREAMSIZE, *PACMDRVSTREAMSIZE, *LPACMDRVSTREAMSIZE;

typedef struct {
    DWORD cbStruct;
    DWORD fdwSuggest;
    LPWAVEFORMATEX pwfxSrc;
    DWORD cbwfxSrc;
    LPWAVEFORMATEX pwfxDst;
    DWORD cbwfxDst;
} ACMDRVFORMATSUGGEST, *PACMDRVFORMATSUGGEST, *LPACMDRVFORMATSUGGEST;

#pragma pack(pop)

static_assert(sizeof(ACMDRVSTREAMHEADER) == sizeof(ACMSTREAMHEADER));

#endif
