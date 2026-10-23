/*
 * direct3d 10 interfaces of d3d11.dll objects: each object carries a second
 * vtable that converts direct3d 10 arguments and forwards to its direct3d 11
 * methods, the device's going to its immediate context.
 */
#ifndef D3D10_PRIVATE_H
#define D3D10_PRIVATE_H

#include "d3d11_private.h"
#include <mime/win32/d3d10_1.h>

/* the interface pointer points at the object's d3d10 field. */
#define D3D10_IMPL(type, iface) CONTAINING_RECORD((void *)(iface), type, d3d10)

/* queries the direct3d 10 interface and releases the direct3d 11 pointer. */
void *D3d10From11(void *iface, REFIID riid);

#endif
