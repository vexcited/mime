/*
 * what the acm codecs share: the messages every installable driver
 * answers alike, and the copy of their details.
 */
#ifndef MIME_LIB_ACMDRV_H
#define MIME_LIB_ACMDRV_H

#include <mime/win32/winbase.h>
#include <mime/win32/msacmdrv.h>

/* what DefDriverProc answers, without importing winmm for it. */
static inline LRESULT AcmdDefault(UINT message)
{
    switch (message) {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_ENABLE:
    case DRV_DISABLE:
        return 1;
    case DRV_CONFIGURE:
    case DRV_QUERYCONFIGURE:
        return 0;
    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_RESTART;
    default:
        return message >= DRV_USER ? MMSYSERR_NOTSUPPORTED : 0;
    }
}

/*
 * the driver's details cut to the caller's size, its strings only when the
 * size reaches past the icon, the long ones only past the long name.
 */
static inline LRESULT AcmdDetails(ACMDRIVERDETAILSW *out, WORD pid, DWORD version, DWORD support, DWORD tags,
                                  const WCHAR *short_name, const WCHAR *long_name, const WCHAR *copyright,
                                  const WCHAR *features)
{
    ACMDRIVERDETAILSW d = {};
    DWORD size = out->cbStruct < sizeof(d) ? out->cbStruct : sizeof(d);
    d.cbStruct = size;
    d.fccType = ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC;
    d.wMid = 1;
    d.wPid = pid;
    d.vdwACM = 0x03320000;
    d.vdwDriver = version;
    d.fdwSupport = support;
    d.cFormatTags = tags;
    if (size > offsetof(ACMDRIVERDETAILSW, hicon)) {
        lstrcpynW(d.szShortName, short_name, ACMDRIVERDETAILS_SHORTNAME_CHARS);
        lstrcpynW(d.szLongName, long_name, ACMDRIVERDETAILS_LONGNAME_CHARS);
        if (size > offsetof(ACMDRIVERDETAILSW, szCopyright)) {
            lstrcpynW(d.szCopyright, copyright, ACMDRIVERDETAILS_COPYRIGHT_CHARS);
            lstrcpynW(d.szFeatures, features, ACMDRIVERDETAILS_FEATURES_CHARS);
        }
    }
    memcpy(out, &d, size);
    return MMSYSERR_NOERROR;
}

/* the open of a driver id without a description, or of a driver by the acm. */
static inline BOOL AcmdOpenAllowed(ACMDRVOPENDESCW *desc)
{
    return !desc || desc->fccType == ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC;
}

static inline DWORD AcmdPcmBlock(const WAVEFORMATEX *f)
{
    return (DWORD)(f->wBitsPerSample >> 3) << (f->nChannels >> 1);
}

#endif
