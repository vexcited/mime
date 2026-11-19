/*
 * evr.dll, the enhanced video renderer. The format helpers are mfplat's, as in Windows, where evr.dll only hands the
 * call on. The answers were read from evr.dll of Windows 11 with test programs.
 */
#include "evr_private.h"

int _fltused = 0x9875;

HINSTANCE EvrInstance;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        EvrInstance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

#define FCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)

/* the formats evr calls yuv, read by trying every format with evrtab.exe. NV21, I420 and YVYU are not among them. */
BOOL WINAPI EvrIsFormatYUV(DWORD format)
{
    static const DWORD YUV[] = {
        FCC('4', '2', '0', 'O'), FCC('A', 'I', '4', '4'), FCC('A', 'Y', 'U', 'V'), FCC('I', 'A', '4', '4'),
        FCC('I', 'M', 'C', '1'), FCC('I', 'M', 'C', '2'), FCC('I', 'M', 'C', '3'), FCC('I', 'M', 'C', '4'),
        FCC('N', 'V', '1', '1'), FCC('N', 'V', '1', '2'), FCC('N', 'V', '2', '4'), FCC('P', '0', '1', '0'),
        FCC('P', '0', '1', '6'), FCC('P', '2', '0', '8'), FCC('P', '2', '1', '0'), FCC('P', '2', '1', '6'),
        FCC('P', '4', '0', '8'), FCC('S', '3', '4', '0'), FCC('S', '3', '4', '2'), FCC('U', 'Y', 'V', 'Y'),
        FCC('Y', '2', '1', '0'), FCC('Y', '2', '1', '6'), FCC('Y', 'U', 'Y', '2'), FCC('Y', 'V', '1', '2'),
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(YUV); i++)
        if (YUV[i] == format)
            return TRUE;
    return FALSE;
}

HRESULT WINAPI EvrGetStrideForBitmapInfoHeader(DWORD format, DWORD width, LONG *stride)
{
    return MFGetStrideForBitmapInfoHeader(format, width, stride);
}

HRESULT WINAPI EvrGetPlaneSize(DWORD format, DWORD width, DWORD height, DWORD *size)
{
    return MFGetPlaneSize(format, width, height, size);
}

HRESULT WINAPI EvrCopyImage(BYTE *dest, LONG dest_stride, const BYTE *src, LONG src_stride, DWORD width, DWORD lines)
{
    return MFCopyImage(dest, dest_stride, src, src_stride, width, lines);
}

HRESULT WINAPI EvrConvertToFP16Array(WORD *dest, const float *src, DWORD count)
{
    return MFConvertToFP16Array(dest, src, count);
}

HRESULT WINAPI EvrConvertFromFP16Array(float *dest, const WORD *src, DWORD count)
{
    return MFConvertFromFP16Array(dest, src, count);
}

HRESULT WINAPI EvrConvertColorInfoToDXVA(DWORD *dxva, const MFVIDEOFORMAT *format)
{
    return MFConvertColorInfoToDXVA(dxva, format);
}

HRESULT WINAPI EvrConvertColorInfoFromDXVA(MFVIDEOFORMAT *format, DWORD dxva)
{
    return MFConvertColorInfoFromDXVA(format, dxva);
}

HRESULT WINAPI EvrInitVideoFormat_RGB(MFVIDEOFORMAT *format, DWORD width, DWORD height, DWORD d3dfmt)
{
    return MFInitVideoFormat_RGB(format, width, height, d3dfmt);
}

HRESULT WINAPI EvrInitVideoFormat(MFVIDEOFORMAT *format, DWORD type)
{
    return MFInitVideoFormat(format, type);
}

DWORD WINAPI EvrGetUncompressedVideoFormat(const MFVIDEOFORMAT *format)
{
    return MFGetUncompressedVideoFormat(format);
}

HRESULT WINAPI EvrCreateVideoMediaType(const MFVIDEOFORMAT *format, IMFMediaType **type)
{
    return MFCreateVideoMediaType(format, type);
}

HRESULT WINAPI EvrCreateVideoMediaTypeFromSubtype(const GUID *subtype, IMFMediaType **type)
{
    return MFCreateVideoMediaTypeFromSubtype(subtype, type);
}

HRESULT WINAPI EvrCreateVideoMediaTypeFromVideoInfoHeader(const VIDEOINFOHEADER *vih, DWORD size, DWORD par_x,
                                                          DWORD par_y, MFVideoInterlaceMode interlace, QWORD flags,
                                                          const GUID *subtype, IMFMediaType **type)
{
    return MFCreateVideoMediaTypeFromVideoInfoHeader(vih, size, par_x, par_y, interlace, flags, subtype, type);
}

HRESULT WINAPI EvrCreateVideoMediaTypeFromVideoInfoHeader2(const VIDEOINFOHEADER2 *vih, DWORD size, QWORD flags,
                                                           const GUID *subtype, IMFMediaType **type)
{
    return MFCreateVideoMediaTypeFromVideoInfoHeader2(vih, size, flags, subtype, type);
}

HRESULT WINAPI EvrCreateDXSurfaceBuffer(REFIID riid, IUnknown *surface, BOOL bottom_up, IMFMediaBuffer **buffer)
{
    return MFCreateDXSurfaceBuffer(riid, surface, bottom_up, buffer);
}

/* windows 11 refuses this one outright. */
HRESULT WINAPI MFCreateVideoMediaTypeFromBitMapInfoHeader(const BITMAPINFOHEADER *header, DWORD par_x, DWORD par_y,
                                                          MFVideoInterlaceMode interlace, QWORD flags, QWORD rate,
                                                          QWORD scale, DWORD bitrate, IMFMediaType **type)
{
    return E_FAIL;
}
