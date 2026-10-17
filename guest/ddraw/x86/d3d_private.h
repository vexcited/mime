/*
 * direct3d 1 to 7, which 32-bit windows has in d3dim.dll and d3dim700.dll
 * and mime in ddraw itself: what its parts share. devices keep the state
 * of their interface, as windows' do, and hand it to the direct3d 9
 * device of d3dback.c in that api's terms.
 */
#ifndef DDRAW_D3D_PRIVATE_H
#define DDRAW_D3D_PRIVATE_H

#include "../ddraw_private.h"
#include <mime/win32/d3d.h>
#include "d3dback.h"

typedef struct D3DOBJ D3DOBJ;
typedef struct D3DDEV D3DDEV;
typedef struct D3DVB D3DVB;

/* ------------------------------------------------------------------ the direct3d object */

struct D3DOBJ {
    IDirect3D7 iface7;
    DDRAW *dd;
    DWORD outer;         /* the directdraw interface whose count it shares */
    LIST_ENTRY devices;
    LIST_ENTRY buffers;  /* vertex buffers, which hold no reference on it */
};

HRESULT D3dObjQuery(DDRAW *dd, DWORD version, REFIID riid, void **out);
void D3dObjDestroy(DDRAW *dd);
D3DOBJ *D3dObjFromIface7(IDirect3D7 *iface);

/* the descriptions of the devices mime offers, and the formats they draw on and sample. */
BOOL D3dDeviceDesc7(const GUID *guid, D3DDEVICEDESC7 *desc);
BOOL D3dIsDeviceGuid(const GUID *guid);
BOOL D3dIsHalGuid(const GUID *guid);
extern const DDPIXELFORMAT D3dTextureFormats[];
extern const DWORD D3dTextureFormatCount;

/* ------------------------------------------------------------------ devices */

#define D3D_STAGES 8
#define D3D_CLIP_PLANES 6

typedef struct D3DLIGHTSLOT {
    D3DLIGHT7 light;
    BOOL set, enabled;
} D3DLIGHTSLOT;

/* an item of a state block, as windows records it: what was set and its value. */
enum { D3DI_RS, D3DI_TSS, D3DI_TEXTURE, D3DI_VIEWPORT, D3DI_MATERIAL, D3DI_LIGHT, D3DI_LIGHTENABLE, D3DI_TRANSFORM,
       D3DI_CLIPPLANE };

typedef struct D3DITEM {
    DWORD op, a, b;
    union {
        DWORD value;          /* of a texture stage state */
        DDSURF *texture;      /* referenced, unlike windows' raw pointer */
        D3DVIEWPORT7 viewport;
        D3DMATERIAL7 material;
        D3DLIGHT7 light;
        D3DMATRIX matrix;
        float plane[4];
    };
} D3DITEM;

typedef struct D3DBLOCK {
    D3DITEM *items;
    DWORD count, capacity;
} D3DBLOCK;

struct D3DDEV {
    IDirect3DDevice7 iface7;
    LONG ref;
    D3DOBJ *d3d;
    GUID guid;
    BOOL hal;
    D3DDEVICEDESC7 desc;
    DbDevice *db;
    DDSURF *target, *depth;
    DDPAL *palette;
    BOOL in_scene;

    DWORD rs[256];
    DWORD tss[D3D_STAGES][32];
    DDSURF *textures[D3D_STAGES];
    D3DMATRIX transforms[24];  /* by type, WORLD 1 to TEXTURE7 23 */
    D3DVIEWPORT7 viewport;
    D3DMATERIAL7 material;
    D3DLIGHTSLOT *lights;
    DWORD light_count;
    float clip_planes[D3D_CLIP_PLANES][4];
    D3DCLIPSTATUS clip_status;

    D3DBLOCK **blocks;         /* by handle, null when free */
    DWORD block_count;
    D3DBLOCK *recording;
    DWORD recording_handle;

    /* what the backend shows of this device is stale after another device drew. */
    BOOL keyed;               /* the key test the color key added is on */
    DWORD stage_end;          /* the stage disabled for lack of its texture, D3D_STAGES for none */
    BYTE *scratch;            /* vertices packed for the backend */
    SIZE_T scratch_size;
    LIST_ENTRY links;
};

HRESULT D3dCreateDevice7(D3DOBJ *d3d, const GUID *guid, DDSURF *target, IDirect3DDevice7 **out);
D3DDEV *D3dDevFromIface7(void *iface);
/* the device whose state the backend holds now, which another device's work takes over. */
void D3dDevActivate(D3DDEV *dev);

/* the size of a vertex of a flexible vertex format, 0 for one that is not valid. */
DWORD D3dFvfSize(DWORD fvf);
DWORD D3dPrimitiveCount(D3DPRIMITIVETYPE type, DWORD count);
/* draws vertices packed as the format says, with optional indices. */
HRESULT D3dDraw(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices, DWORD vertex_count,
                const WORD *indices, DWORD index_count, DWORD flags);
/* strided vertices packed in the device's scratch memory as the format lays them out. */
const BYTE *D3dPackStrided(D3DDEV *dev, DWORD fvf, const D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD count);
/* pipeline.c: vertices through the device's transforms, lighting and fog into screen space. */
HRESULT D3dProcess(D3DDEV *dev, const BYTE *src, DWORD src_fvf, BYTE *dst, DWORD dst_fvf, DWORD count, DWORD op,
                   DWORD flags);

/* ------------------------------------------------------------------ surfaces in the backend */

/* a surface's copy in the backend, which travels with its memory when it flips. */
typedef struct D3DSURFX {
    DbImage *image;
    DWORD usage;          /* DB_ flags it was made for */
    BOOL gpu_newer;       /* drawn to since the memory was read */
    DWORD generation;     /* of the memory when the two last matched */
    BOOL converted;       /* uploaded through DB_FORMAT_ARGB */
    BOOL keyed;           /* with its color key as alpha */
    DWORD key;            /* the key it was uploaded with */
    DWORD palette;        /* the palette's uniqueness it was uploaded with */
    /* a depth fill waiting for the buffer to be bound, the backend clearing only what is. */
    BOOL fill;
    float fill_z;
    DWORD fill_stencil;
    RECT fill_rect;
} D3DSURFX;

/* the image of a surface, current for the use asked; null when it cannot have one. */
DbImage *D3dSurfImage(D3DDEV *dev, DDSURF *s, DWORD usage);
/* the surface was drawn to on the gpu. */
void D3dSurfDrawn(DDSURF *s);
/* a clear covering a whole target makes its memory irrelevant. */
void D3dSurfReplaced(DDSURF *s);

/* ------------------------------------------------------------------ vertex buffers */

struct D3DVB {
    IDirect3DVertexBuffer7 iface7;
    LONG ref;
    D3DOBJ *d3d;
    D3DVERTEXBUFFERDESC desc;
    DWORD stride;
    BYTE *data;
    DWORD locks;
    BOOL optimized;
    LIST_ENTRY links;
};

HRESULT D3dCreateVertexBuffer7(D3DOBJ *d3d, const D3DVERTEXBUFFERDESC *desc, DWORD flags,
                               IDirect3DVertexBuffer7 **out);
D3DVB *D3dVbFromIface7(void *iface);

#endif
