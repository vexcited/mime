/* the DirectShow video format blocks. */
#ifndef MIME_WIN32_AMVIDEO_H
#define MIME_WIN32_AMVIDEO_H

#include <mime/win32/wingdi.h>

#ifndef MIME_REFERENCE_TIME_DEFINED
#define MIME_REFERENCE_TIME_DEFINED
typedef LONGLONG REFERENCE_TIME;
#endif

typedef struct tagVIDEOINFOHEADER {
    RECT rcSource;
    RECT rcTarget;
    DWORD dwBitRate;
    DWORD dwBitErrorRate;
    REFERENCE_TIME AvgTimePerFrame;
    BITMAPINFOHEADER bmiHeader;
} VIDEOINFOHEADER;

typedef struct tagVIDEOINFOHEADER2 {
    RECT rcSource;
    RECT rcTarget;
    DWORD dwBitRate;
    DWORD dwBitErrorRate;
    REFERENCE_TIME AvgTimePerFrame;
    DWORD dwInterlaceFlags;
    DWORD dwCopyProtectFlags;
    DWORD dwPictAspectRatioX;
    DWORD dwPictAspectRatioY;
    union {
        DWORD dwControlFlags;
        DWORD dwReserved1;
    };
    DWORD dwReserved2;
    BITMAPINFOHEADER bmiHeader;
} VIDEOINFOHEADER2;

#define AMINTERLACE_IsInterlaced 0x00000001
#define AMINTERLACE_1FieldPerSample 0x00000002
#define AMINTERLACE_Field1First 0x00000004
#define AMINTERLACE_FieldPatternMask 0x00000030
#define AMINTERLACE_DisplayModeMask 0x000000C0
#define AMINTERLACE_DisplayModeBobOnly 0x00000000
#define AMINTERLACE_DisplayModeWeaveOnly 0x00000040
#define AMINTERLACE_DisplayModeBobOrWeave 0x00000080

#define AMCONTROL_USED 0x00000001
#define AMCONTROL_PAD_TO_4x3 0x00000002
#define AMCONTROL_PAD_TO_16x9 0x00000004
#define AMCONTROL_COLORINFO_PRESENT 0x00000080

#endif
