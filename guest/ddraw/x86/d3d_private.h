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
    IDirect3D iface1;
    IDirect3D2 iface2;
    IDirect3D3 iface3;
    DDRAW *dd;
    DWORD outer;         /* the directdraw interface whose count it shares */
    LIST_ENTRY devices;
    LIST_ENTRY buffers;  /* vertex buffers, which hold no reference on it */
    LIST_ENTRY viewports, lights, materials;
    DWORD next_viewport; /* ids of viewports, from 1. */
};

HRESULT D3dObjQuery(DDRAW *dd, DWORD version, REFIID riid, void **out);
void D3dObjDestroy(DDRAW *dd);
void D3dLegacyInitObj(D3DOBJ *d3d);
D3DOBJ *D3dObjFromIface7(IDirect3D7 *iface);
/* the directdraw object's count, which the direct3d object shares. */
void D3dObjAddRef(D3DOBJ *d3d);
void D3dObjRelease(D3DOBJ *d3d);

/* the descriptions of the devices mime offers, and the formats they draw on and sample. */
BOOL D3dDeviceDesc7(const GUID *guid, D3DDEVICEDESC7 *desc);
void D3dPrimCaps(D3DPRIMCAPS *caps, BOOL hal);
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

typedef struct D3DLEGACY D3DLEGACY;

struct D3DDEV {
    IDirect3DDevice7 iface7;
    IDirect3DDevice iface1;
    IDirect3DDevice2 iface2;
    IDirect3DDevice3 iface3;
    IUnknown inner;           /* its own unknown, which a surface aggregating a first device holds. */
    IUnknown *outer;          /* where the interfaces' IUnknown methods go. */
    LONG ref;
    DWORD version;            /* of the interface that made it: 1, 2, 3 or 7. */
    DWORD surface_version;    /* of the surface interfaces it holds, 0 for the weak ones of an aggregate. */
    D3DLEGACY *legacy;        /* what the first interfaces add, null for the seventh. */
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
    DWORD weak_textures;       /* stages whose texture came through a handle, holding no reference. */
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
/* the state of a device, the backend's when it is the one drawing. */
void D3dSetRs(D3DDEV *dev, DWORD type, DWORD value);
void D3dSetTss(D3DDEV *dev, DWORD stage, DWORD type, DWORD value);
void D3dSetTransform(D3DDEV *dev, DWORD type, const D3DMATRIX *m);
void D3dSetViewport(D3DDEV *dev, const D3DVIEWPORT7 *vp);
void D3dSetMaterial(D3DDEV *dev, const D3DMATERIAL7 *m);
D3DLIGHTSLOT *D3dLightSlot(D3DDEV *dev, DWORD index);
HRESULT D3dSetLight(D3DDEV *dev, DWORD index, const D3DLIGHT7 *light);
HRESULT D3dLightEnable(D3DDEV *dev, DWORD index, BOOL enable);
void D3dSetTexture(D3DDEV *dev, DWORD stage, DDSURF *s);
void D3dSetTextureWeak(D3DDEV *dev, DWORD stage, DDSURF *s);
/* the projection again, after the clip volume of a first interface's viewport changed. */
void D3dPushProjection(D3DDEV *dev);
BOOL D3dBindTargets(D3DDEV *dev);
DDSURF *D3dAttachedDepth(DDSURF *s);
DWORD D3dDdbd(DWORD bits);
BOOL D3dPalettized(const DDSURF *s);
HRESULT D3dCheckPalette(DDSURF *target);
void D3dHoldTargets(D3DDEV *dev, DDSURF *target, DDSURF *depth);
D3DMATRIX D3dMultiply(const D3DMATRIX *a, const D3DMATRIX *b);
/* the rectangles given, already within the viewport, cleared on the targets. */
HRESULT D3dClearRects(D3DDEV *dev, DWORD count, const D3DRECT *rects, DWORD flags, DWORD color, float z,
                      DWORD stencil);
/* a device's end: its references, textures and the first interfaces' objects released. */
void D3dDevDestroy(D3DDEV *dev);
/* the device whose state the backend holds now, which another device's work takes over. */
void D3dDevActivate(D3DDEV *dev);
BOOL D3dDevIsActive(D3DDEV *dev);

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

/* ------------------------------------------------------------------ the first interfaces, legacy.c */

typedef struct D3DVIEWPORTOBJ D3DVIEWPORTOBJ;
typedef struct D3DLIGHTOBJ D3DLIGHTOBJ;
typedef struct D3DMATERIALOBJ D3DMATERIALOBJ;
typedef struct D3DTEXOBJ D3DTEXOBJ;
typedef struct D3DEXECBUF D3DEXECBUF;

/* a viewport, one object for its three interfaces, and the lights it holds. */
struct D3DVIEWPORTOBJ {
    IDirect3DViewport3 iface;
    LONG ref;
    D3DOBJ *d3d;
    D3DDEV *dev;              /* the device it was added to. */
    DWORD id;
    LIST_ENTRY lights;        /* D3DLIGHTOBJ, in the order added. */
    DWORD light_count;
    BOOL lights_dirty;
    D3DVIEWPORT2 data;
    BOOL set;
    BOOL background_set;
    D3DMATERIALHANDLE background;
    IDirectDrawSurface *background_depth;
    LIST_ENTRY links;         /* in the direct3d object's list. */
    LIST_ENTRY dev_links;     /* in its device's list. */
};

struct D3DLIGHTOBJ {
    IDirect3DLight iface;
    LONG ref;
    D3DOBJ *d3d;
    D3DLIGHT2 data;           /* as set, a D3DLIGHT taking the first 76 bytes. */
    BOOL set;
    D3DVIEWPORTOBJ *viewport;
    LIST_ENTRY vp_links;
    LIST_ENTRY links;
};

struct D3DMATERIALOBJ {
    IDirect3DMaterial iface1;
    IDirect3DMaterial2 iface2;
    IDirect3DMaterial3 iface3;
    LONG ref;
    D3DOBJ *d3d;
    D3DMATERIAL data;
    LIST_ENTRY handles;       /* D3DMATHANDLE of the devices it has a handle on. */
    LIST_ENTRY links;
};

/* what a material handle names: the material's copy on a device. */
typedef struct D3DMATHANDLE {
    D3DMATERIALOBJ *material;
    D3DDEV *dev;
    D3DMATERIAL data;
    LIST_ENTRY mat_links;
    LIST_ENTRY dev_links;
} D3DMATHANDLE;

/* IDirect3DTexture and 2, aggregated in a surface. */
struct D3DTEXOBJ {
    IDirect3DTexture iface1;
    IDirect3DTexture2 iface2;
    IUnknown inner;
    LONG ref;
    DDSURF *surface;
    LIST_ENTRY handles;       /* D3DTEXHANDLE. */
};

typedef struct D3DTEXHANDLE {
    D3DTEXOBJ *texture;
    D3DDEV *dev;
    LIST_ENTRY tex_links;
    LIST_ENTRY dev_links;
} D3DTEXHANDLE;

typedef struct D3DMATRIXOBJ {
    D3DMATRIX m;
    LIST_ENTRY links;
} D3DMATRIXOBJ;

struct D3DLEGACY {
    D3DDEVICEDESC hw, sw;
    LIST_ENTRY viewports;     /* D3DVIEWPORTOBJ.dev_links. */
    DWORD viewport_count;
    D3DVIEWPORTOBJ *current;
    DWORD downloaded;         /* the id of the viewport last downloaded. */
    D3DMATRIX clip_map;
    DWORD light_states[9];
    DWORD light_locks, transform_locks, rs_locks[8];
    D3DMATERIAL material;     /* the current material's values. */
    LIST_ENTRY materials;     /* D3DMATHANDLE.dev_links. */
    LIST_ENTRY textures;      /* D3DTEXHANDLE.dev_links. */
    LIST_ENTRY matrices;      /* D3DMATRIXOBJ. */
    LIST_ENTRY buffers;       /* D3DEXECBUF. */
    DWORD bound[4];           /* the matrix handles bound as world, view and projection. */
    D3DSTATS stats;
    DWORD status;             /* clip status and extents, shared by execute buffers and GetClipStatus. */
    float extents[4];
    /* the stage states the third interface reads back, which the legacy mapping leaves alone. */
    DWORD api_tss[D3D_STAGES][32];
    D3DTEXOBJ *stage_textures[D3D_STAGES];
    /* between Begin and End. */
    BOOL in_begin;
    D3DPRIMITIVETYPE begin_type;
    DWORD begin_fvf, begin_flags, begin_stride;
    BYTE *begin_vertices;
    DWORD begin_count, begin_capacity;
    WORD *begin_indices;
    DWORD begin_index_count;
    const BYTE *begin_user;   /* the vertices of BeginIndexed. */
    DWORD begin_user_count;
    /* the transformed vertices of execute buffers, with their clip space positions and codes. */
    BYTE *tl;
    float *tl_clip;
    WORD *tl_codes;
    DWORD tl_capacity;
    D3DPICKRECORD *picks;
    DWORD pick_count;
    BYTE *lit;                /* vertices lit on the cpu, as the backend draws them. */
    SIZE_T lit_size;
};

/* an execute buffer, its memory the device's to read at Execute. */
struct D3DEXECBUF {
    IDirect3DExecuteBuffer iface;
    LONG ref;
    D3DDEV *dev;
    BYTE *memory;
    DWORD size;
    BOOL locked;
    DWORD locker;             /* the process that locked it. */
    D3DEXECUTEDATA data;
    LIST_ENTRY links;
};

/* legacy.c: the direct3d object and devices of the first interfaces. */
void D3dLegacyActivate(D3DDEV *dev);
void D3dLegacyDestroy(D3DDEV *dev);
/* the clip volume of the current viewport, after the projection. */
const D3DMATRIX *D3dLegacyClipMap(D3DDEV *dev);
/* the viewport downloaded: its clip volume and rectangle made the device's. */
HRESULT D3dLegacyDownload(D3DDEV *dev, D3DVIEWPORTOBJ *vp);
/* the current viewport downloaded if another one was last. */
HRESULT D3dLegacyDownloadCurrent(D3DDEV *dev);
/* the device's render state through the rules of the first interfaces, legacy states mapped to stages. */
HRESULT D3dLegacySetRenderState(D3DDEV *dev, DWORD type, DWORD value);
HRESULT D3dLegacySetLightState(D3DDEV *dev, DWORD type, DWORD value);
/* a stage state of the third interface, kept apart from what the legacy mapping makes of it. */
void D3dLegacySetTss(D3DDEV *dev, DWORD stage, DWORD type, DWORD value);
/* the stage textures a device of the third interface binds. */
void D3dLegacySetTexture(D3DDEV *dev, DWORD stage, D3DTEXOBJ *texture);
/* the device descriptions of a device guid for an interface version. */
BOOL D3dLegacyDesc(const GUID *guid, DWORD version, D3DDEVICEDESC *hw, D3DDEVICEDESC *sw);

/* legacydev.c: the interfaces of a device. */
void D3dLegacyDeviceInit(D3DDEV *dev);
D3DDEV *D3dDevFromLegacy(void *iface);
/* vertices of a format drawn, lit on the cpu when they have normals and a material is current. */
HRESULT D3dLegacyDraw(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices, DWORD vertex_count,
                      const WORD *indices, DWORD index_count, DWORD flags);
/* the same without the viewport's download or the statistics, for what execute buffers draw. */
HRESULT D3dLegacyRender(D3DDEV *dev, D3DPRIMITIVETYPE type, DWORD fvf, const void *vertices, DWORD vertex_count,
                        const WORD *indices, DWORD index_count, DWORD flags);
/* a matrix handle of the first interface, and its change reaching the transforms it is bound to. */
D3DMATRIXOBJ *D3dMatrixHandle(D3DDEV *dev, DWORD handle);
void D3dMatrixChanged(D3DDEV *dev, DWORD handle);

/* objects.c: viewports, lights and materials. */
HRESULT D3dCreateViewport(D3DOBJ *d3d, D3DVIEWPORTOBJ **out);
HRESULT D3dCreateLightObj(D3DOBJ *d3d, D3DLIGHTOBJ **out);
HRESULT D3dCreateMaterialObj(D3DOBJ *d3d, D3DMATERIALOBJ **out);
D3DVIEWPORTOBJ *D3dViewportFrom(void *iface);
D3DLIGHTOBJ *D3dLightFrom(void *iface);
D3DMATERIALOBJ *D3dMaterialFrom(void *iface);
D3DMATHANDLE *D3dMaterialHandle(D3DDEV *dev, DWORD handle);
void D3dMaterialDeviceGone(D3DMATHANDLE *h);
HRESULT D3dViewportClear(D3DVIEWPORTOBJ *vp, DWORD count, const D3DRECT *rects, DWORD flags, DWORD color, float z,
                         DWORD stencil, BOOL second);

/* texture.c: IDirect3DTexture and its handles. */
HRESULT D3dCreateTextureForSurface(DDSURF *surface, REFIID riid, IUnknown **inner);
D3DTEXOBJ *D3dTextureFrom(void *iface);
D3DTEXHANDLE *D3dTextureHandle(D3DDEV *dev, DWORD handle);
void D3dTextureDeviceGone(D3DTEXHANDLE *h);
HRESULT D3dTextureLoad(DDSURF *dst, DDSURF *src);

/* execute.c: execute buffers and picking. */
HRESULT D3dCreateExecuteBuffer(D3DDEV *dev, const D3DEXECUTEBUFFERDESC *desc, IDirect3DExecuteBuffer **out);
D3DEXECBUF *D3dExecuteBufferFrom(void *iface);
HRESULT D3dExecute(D3DDEV *dev, D3DEXECBUF *buffer, DWORD flags);
HRESULT D3dPick(D3DDEV *dev, D3DEXECBUF *buffer, const D3DRECT *rect);
void D3dExecuteBufferDeviceGone(D3DEXECBUF *b);

/* pipeline.c: the first interfaces' lighting and vertex fog. */
/* the colors of lit vertices, diffuse and specular, written at each output vertex. */
void D3dLegacyLight(D3DDEV *dev, const BYTE *src, DWORD fvf, DWORD count, BYTE *dst, DWORD dst_stride);
/* the vertex fog of a camera space depth as a specular alpha, 255 without fog. */
DWORD D3dLegacyFog(D3DDEV *dev, float z);
/* the first interface's ProcessVertices: to the screen through the current viewport, lit if asked. */
HRESULT D3dLegacyProcess(D3DDEV *dev, const BYTE *src, DWORD src_fvf, BYTE *dst, DWORD dst_fvf, DWORD count,
                         DWORD op);

/* ------------------------------------------------------------------ surfaces in the backend */

/* a direct3d object aggregated in a surface, its inner unknown held until the surface goes. */
typedef struct D3DAGG {
    IUnknown *inner;
    struct D3DAGG *next;
} D3DAGG;

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
    IDirect3DVertexBuffer iface1;
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
/* the first interface's buffer, the same object behind another vtable. */
HRESULT D3dCreateVertexBuffer1(D3DOBJ *d3d, const D3DVERTEXBUFFERDESC *desc, DWORD flags,
                               IDirect3DVertexBuffer **out);
D3DVB *D3dVbFromIface1(void *iface);

#endif
