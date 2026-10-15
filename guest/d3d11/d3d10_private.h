/*
 * the direct3d 10 interfaces of d3d11.dll's objects: each object carries
 * a second vtable, which converts what direct3d 10 passes and forwards to
 * the object's direct3d 11 methods, the device's to its immediate context.
 */
#ifndef D3D10_PRIVATE_H
#define D3D10_PRIVATE_H

#include "d3d11_private.h"
#include <mime/win32/d3d10_1.h>

/* the object behind a direct3d 10 interface pointer, which points at its d3d10 field. */
#define D3D10_IMPL(type, iface) CONTAINING_RECORD((void *)(iface), type, d3d10)

/* the direct3d 10 interface of what a direct3d 11 method handed out, which loses its reference. */
void *D3d10From11(void *iface, REFIID riid);

#endif
