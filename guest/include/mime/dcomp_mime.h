/*
 * what dcomp.dll offers beyond windows' interfaces: a way for tests to read
 * the pixels of the frame last composed into a target's window, which the
 * host shows but a program cannot read back from the window itself.
 */
#ifndef MIME_DCOMP_MIME_H
#define MIME_DCOMP_MIME_H

#include <mime/win32/dcomp.h>

typedef struct IMimeDCompositionDevice IMimeDCompositionDevice;
#define IMimeDCompositionDevice_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    /* BGRA bytes of a rectangle of the target's last frame; S_FALSE when nothing was composed yet. */               \
    HRESULT(STDMETHODCALLTYPE *ReadTarget)(T * This, IDCompositionTarget * target, const RECT *rect, void *pixels,     \
                                           UINT pitch);
MIME_COM_INTERFACE(IMimeDCompositionDevice)
MIME_DEFINE_GUID(IID_IMimeDCompositionDevice, 0x5A0E2C8B, 0x91D3, 0x4C6E, 0xA4, 0x17, 0x6B, 0xD2, 0xF3, 0x08, 0x7E, 0x51);

#endif
