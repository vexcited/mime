/*
 * direct3d 1 to 7 draw on a hidden direct3d 9 device owned by the process.
 * d3dim devices hand their state here in direct3d 9 terms, and surfaces
 * direct3d draws to or samples keep a copy here. this header uses neither
 * api's types so both sides can include it.
 */
#ifndef DDRAW_D3DBACK_H
#define DDRAW_D3DBACK_H

#include <mime/win32/ddraw.h>

typedef struct DbDevice DbDevice;
typedef struct DbImage DbImage;

enum {
    DB_TEXTURE = 1,     /* sampled; the cpu writes its levels. */
    DB_TARGET = 2,      /* drawn to, and sampled when also a texture. */
    DB_DEPTH = 4,
    DB_CUBE = 8,
};

/* direct3d 9 numbers for the few states direct3d 7 numbers differently. */
enum {
    DB_RS_DEPTHBIAS = 195,
    DB_RS_ANTIALIASEDLINEENABLE = 176,
    DB_RS_POINTSIZE_MIN = 155,
    DB_SAMP_ADDRESSU = 1,
    DB_SAMP_ADDRESSV = 2,
    DB_SAMP_ADDRESSW = 3,
    DB_SAMP_BORDERCOLOR = 4,
    DB_SAMP_MAGFILTER = 5,
    DB_SAMP_MINFILTER = 6,
    DB_SAMP_MIPFILTER = 7,
    DB_SAMP_MIPMAPLODBIAS = 8,
    DB_SAMP_MAXMIPLEVEL = 9,
    DB_SAMP_MAXANISOTROPY = 10,
    DB_TS_WORLD = 256,
};

/* created on first use; null when direct3d 9 is missing. */
DbDevice *DbDeviceGet(void);

/*
 * the direct3d 9 format a surface layout copies to unchanged, 0 for none.
 * callers convert layouts without one and use DB_FORMAT_ARGB.
 */
DWORD DbFormatOf(const DDPIXELFORMAT *pf, DWORD usage);
#define DB_FORMAT_ARGB 21 /* D3DFMT_A8R8G8B8 */
#define DB_FORMAT_XRGB 22 /* D3DFMT_X8R8G8B8 */

DbImage *DbImageCreate(DbDevice *dev, DWORD usage, UINT width, UINT height, UINT levels, DWORD format);
void DbImageDestroy(DbImage *image);
DWORD DbImageFormat(const DbImage *image);
/* rows of a level or cube face level, in the image's format. */
BOOL DbImageWrite(DbImage *image, UINT face, UINT level, const RECT *rect, const void *data, UINT pitch);
BOOL DbImageRead(DbImage *image, const RECT *rect, void *data, UINT pitch);

/* state, in direct3d 9 terms. */
void DbSetTargets(DbDevice *dev, DbImage *target, DbImage *depth);
void DbSetRenderState(DbDevice *dev, DWORD state, DWORD value);
void DbSetTextureStageState(DbDevice *dev, DWORD stage, DWORD state, DWORD value);
void DbSetSamplerState(DbDevice *dev, DWORD stage, DWORD state, DWORD value);
void DbSetTexture(DbDevice *dev, DWORD stage, DbImage *image);
void DbSetTransform(DbDevice *dev, DWORD state, const float *matrix);
void DbSetViewport(DbDevice *dev, DWORD x, DWORD y, DWORD width, DWORD height, float min_z, float max_z);
/* D3DMATERIAL7 has the same 68 bytes as D3DMATERIAL9. */
void DbSetMaterial(DbDevice *dev, const void *material);
/* D3DLIGHT7 has the same 104 bytes as D3DLIGHT9. */
void DbSetLight(DbDevice *dev, DWORD index, const void *light);
void DbLightEnable(DbDevice *dev, DWORD index, BOOL enable);
void DbSetClipPlane(DbDevice *dev, DWORD index, const float *plane);

/* rects of four LONGs, x2 and y2 exclusive. */
HRESULT DbClear(DbDevice *dev, DWORD count, const void *rects, DWORD flags, DWORD color, float z, DWORD stencil);
HRESULT DbDraw(DbDevice *dev, DWORD type, DWORD primitives, const void *vertices, DWORD stride, DWORD fvf);
HRESULT DbDrawIndexed(DbDevice *dev, DWORD type, DWORD primitives, DWORD vertex_count, const WORD *indices,
                      const void *vertices, DWORD stride, DWORD fvf);

#endif
