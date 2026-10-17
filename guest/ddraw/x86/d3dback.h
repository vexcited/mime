/*
 * where direct3d 1 to 7 draw: a direct3d 9 device of the process's own,
 * which no window shows. the devices of d3dim keep their state and hand
 * it here in direct3d 9's terms, and surfaces that direct3d draws to or
 * samples keep a copy here. this header knows neither api's types, so
 * the files of either take it.
 */
#ifndef DDRAW_D3DBACK_H
#define DDRAW_D3DBACK_H

#include <mime/win32/ddraw.h>

typedef struct DbDevice DbDevice;
typedef struct DbImage DbImage;

/* what an image is for. */
enum {
    DB_TEXTURE = 1,     /* sampled, its levels written by the cpu */
    DB_TARGET = 2,      /* drawn to, and sampled when also a texture */
    DB_DEPTH = 4,
    DB_CUBE = 8,
};

/* direct3d 9's states of the same names, for the few that direct3d 7 numbers otherwise. */
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

/* the device of the process, made at the first use; null when direct3d 9 is missing. */
DbDevice *DbDeviceGet(void);

/*
 * the direct3d 9 format a surface of this layout is copied to as it is,
 * 0 for none. formats without one go through DB_FORMAT_ARGB after the
 * caller converts them.
 */
DWORD DbFormatOf(const DDPIXELFORMAT *pf, DWORD usage);
#define DB_FORMAT_ARGB 21 /* D3DFMT_A8R8G8B8 */
#define DB_FORMAT_XRGB 22 /* D3DFMT_X8R8G8B8 */

DbImage *DbImageCreate(DbDevice *dev, DWORD usage, UINT width, UINT height, UINT levels, DWORD format);
void DbImageDestroy(DbImage *image);
DWORD DbImageFormat(const DbImage *image);
/* rows of a level or a cube face's level, in the image's format. */
BOOL DbImageWrite(DbImage *image, UINT face, UINT level, const RECT *rect, const void *data, UINT pitch);
/* the pixels of a target, in its format. */
BOOL DbImageRead(DbImage *image, const RECT *rect, void *data, UINT pitch);

/* state, in direct3d 9's terms. */
void DbSetTargets(DbDevice *dev, DbImage *target, DbImage *depth);
void DbSetRenderState(DbDevice *dev, DWORD state, DWORD value);
void DbSetTextureStageState(DbDevice *dev, DWORD stage, DWORD state, DWORD value);
void DbSetSamplerState(DbDevice *dev, DWORD stage, DWORD state, DWORD value);
void DbSetTexture(DbDevice *dev, DWORD stage, DbImage *image);
void DbSetTransform(DbDevice *dev, DWORD state, const float *matrix);
void DbSetViewport(DbDevice *dev, DWORD x, DWORD y, DWORD width, DWORD height, float min_z, float max_z);
/* the 68 bytes of D3DMATERIAL9, D3DMATERIAL7's. */
void DbSetMaterial(DbDevice *dev, const void *material);
/* the 104 bytes of D3DLIGHT9, D3DLIGHT7's. */
void DbSetLight(DbDevice *dev, DWORD index, const void *light);
void DbLightEnable(DbDevice *dev, DWORD index, BOOL enable);
void DbSetClipPlane(DbDevice *dev, DWORD index, const float *plane);

/* rects of four LONGs, x2 and y2 exclusive. */
HRESULT DbClear(DbDevice *dev, DWORD count, const void *rects, DWORD flags, DWORD color, float z, DWORD stencil);
HRESULT DbDraw(DbDevice *dev, DWORD type, DWORD primitives, const void *vertices, DWORD stride, DWORD fvf);
HRESULT DbDrawIndexed(DbDevice *dev, DWORD type, DWORD primitives, DWORD vertex_count, const WORD *indices,
                      const void *vertices, DWORD stride, DWORD fvf);

#endif
