/* the direct2d value types shared by d2d1, d2d1effects and dcomp. */
#ifndef MIME_WIN32_D2DBASETYPES_H
#define MIME_WIN32_D2DBASETYPES_H

#include <mime/win32/windef.h>

typedef struct D2D_POINT_2F {
    float x, y;
} D2D_POINT_2F;

typedef struct D2D_POINT_2U {
    UINT32 x, y;
} D2D_POINT_2U;

typedef struct D2D_POINT_2L {
    INT32 x, y;
} D2D_POINT_2L;

typedef struct D2D_VECTOR_2F {
    float x, y;
} D2D_VECTOR_2F;

typedef struct D2D_VECTOR_3F {
    float x, y, z;
} D2D_VECTOR_3F;

typedef struct D2D_VECTOR_4F {
    float x, y, z, w;
} D2D_VECTOR_4F;

typedef struct D2D_SIZE_F {
    float width, height;
} D2D_SIZE_F;

typedef struct D2D_SIZE_U {
    UINT32 width, height;
} D2D_SIZE_U;



typedef struct D2D_RECT_F {
    float left, top, right, bottom;
} D2D_RECT_F;

typedef struct D2D_RECT_U {
    UINT32 left, top, right, bottom;
} D2D_RECT_U;

typedef struct D2D_RECT_L {
    INT32 left, top, right, bottom;
} D2D_RECT_L;

typedef struct D2D_MATRIX_3X2_F {
    float _11, _12, _21, _22, _31, _32;
} D2D_MATRIX_3X2_F;

typedef struct D2D_MATRIX_4X4_F {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
} D2D_MATRIX_4X4_F;

typedef struct D2D_MATRIX_5X4_F {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
            float _51, _52, _53, _54;
        };
        float m[5][4];
    };
} D2D_MATRIX_5X4_F;

typedef struct D2D1_COLOR_F {
    float r, g, b, a;
} D2D1_COLOR_F;

typedef enum D2D1_BORDER_MODE {
    D2D1_BORDER_MODE_SOFT = 0,
    D2D1_BORDER_MODE_HARD = 1
} D2D1_BORDER_MODE;

typedef enum D2D1_COLORMATRIX_ALPHA_MODE {
    D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED = 1,
    D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT = 2
} D2D1_COLORMATRIX_ALPHA_MODE;

typedef enum D2D1_COLORMANAGEMENT_ALPHA_MODE {
    D2D1_COLORMANAGEMENT_ALPHA_MODE_PREMULTIPLIED = 1,
    D2D1_COLORMANAGEMENT_ALPHA_MODE_STRAIGHT = 2
} D2D1_COLORMANAGEMENT_ALPHA_MODE;

typedef enum D2D1_COLORMANAGEMENT_QUALITY {
    D2D1_COLORMANAGEMENT_QUALITY_PROPER = 0,
    D2D1_COLORMANAGEMENT_QUALITY_NORMAL = 1,
    D2D1_COLORMANAGEMENT_QUALITY_BEST = 2
} D2D1_COLORMANAGEMENT_QUALITY;

typedef enum D2D1_COLORMANAGEMENT_RENDERING_INTENT {
    D2D1_COLORMANAGEMENT_RENDERING_INTENT_PERCEPTUAL = 0,
    D2D1_COLORMANAGEMENT_RENDERING_INTENT_RELATIVE_COLORIMETRIC = 1,
    D2D1_COLORMANAGEMENT_RENDERING_INTENT_SATURATION = 2,
    D2D1_COLORMANAGEMENT_RENDERING_INTENT_ABSOLUTE_COLORIMETRIC = 3
} D2D1_COLORMANAGEMENT_RENDERING_INTENT;


#endif
