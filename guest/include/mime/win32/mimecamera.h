/*
 * mime's capture source extension: the capture engine asks it for frames already in the format its sinks want, so
 * the host converts them. other sources lack it, and the engine then converts the few formats it can itself.
 */
#ifndef MIME_WIN32_MIMECAMERA_H
#define MIME_WIN32_MIMECAMERA_H

#include <mime/win32/mfidl.h>

typedef struct IMimeCaptureSource IMimeCaptureSource;

/*
 * SetOutputSubtype: the subtype a stream sends from its next start, converted from the camera's, or GUID_NULL for
 * the camera's own. CanConvertTo: whether a camera subtype can be converted to another.
 */
#define IMimeCaptureSource_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetOutputSubtype)(T * This, DWORD stream, const GUID *subtype);                         \
    HRESULT(STDMETHODCALLTYPE *CanConvertTo)(T * This, DWORD stream, const GUID *from, const GUID *to);

MIME_COM_INTERFACE(IMimeCaptureSource)

MIME_DEFINE_GUID(IID_IMimeCaptureSource, 0x6D1B6B0A, 0x3A19, 0x4E6C, 0x9C, 0x6F, 0x3F, 0x4B, 0x6A, 0x58, 0x1D, 0x92);

#endif
