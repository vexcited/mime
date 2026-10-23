/* effects loaded from the FX10 chunk of an fx_4_0 container, and the objects they hand out. */
#ifndef D3D10_EFFECT_PRIVATE_H
#define D3D10_EFFECT_PRIVATE_H

#include "d3d10_private.h"

typedef struct FxEffect FxEffect;
typedef struct FxType FxType;
typedef struct FxVariable FxVariable;
typedef struct FxBuffer FxBuffer;
typedef struct FxBlock FxBlock;
typedef struct FxShader FxShader;
typedef struct FxPass FxPass;

/* type kinds, and the fx_4_0 object types. */
enum { FX_NUMERIC = 1, FX_OBJECT = 2, FX_STRUCT = 3 };
enum {
    FX_STRING = 1,
    FX_BLEND = 2,
    FX_DEPTHSTENCIL = 3,
    FX_RASTERIZER = 4,
    FX_PIXELSHADER = 5,
    FX_VERTEXSHADER = 6,
    FX_GEOMETRYSHADER = 7,
    FX_GEOMETRYSHADER_SO = 8,
    FX_TEXTURE = 9,
    FX_TEXTURECUBE = 17,
    FX_CONSTANTBUFFER = 18,
    FX_RENDERTARGETVIEW = 19,
    FX_DEPTHSTENCILVIEW = 20,
    FX_SAMPLER = 21,
    FX_BUFFER = 22,
};

/* numeric type bits: layout, base type, rows, columns and the column major flag. */
#define FX_LAYOUT(n) ((n) & 7)
#define FX_BASE(n) (((n) >> 3) & 7)
#define FX_ROWS(n) (((n) >> 8) & 7)
#define FX_COLUMNS(n) (((n) >> 11) & 7)
#define FX_COLUMN_MAJOR 0x4000
enum { FX_SCALAR = 1, FX_VECTOR = 2, FX_MATRIX = 3 };
enum { FX_FLOAT = 1, FX_INT = 2, FX_UINT = 3, FX_BOOL = 4 };

typedef struct FxMember {
    const char *name, *semantic;
    UINT offset;
    FxType *type;
} FxMember;

struct FxType {
    ID3D10EffectType iface;
    const char *name;
    UINT kind;
    UINT elements, size, stride, packed;
    UINT numeric, object;
    UINT member_count;
    FxMember *members;
    /* an array's element type, or the type itself. */
    FxType *single;
    /* constant buffer types report their own class and type. */
    D3D10_SHADER_VARIABLE_TYPE buffer_type;
};

/* state block kinds, as the state table names them. */
enum {
    FX_BLOCK_DEPTHSTENCIL = 1,
    FX_BLOCK_BLEND = 2,
    FX_BLOCK_RASTERIZER = 3,
    FX_BLOCK_SAMPLER = 4,
    FX_BLOCK_PASS = 5
};

typedef struct FxExpression FxExpression;

typedef struct FxAssignment {
    UINT state, index, kind;
    /* a field of a block or pass. */
    void *target;
    FxVariable *source, *index_source;
    UINT constant_index;
    FxExpression *expression;
} FxAssignment;

struct FxBlock {
    UINT kind;
    union {
        D3D10_DEPTH_STENCIL_DESC depth_stencil;
        D3D10_BLEND_DESC blend;
        D3D10_RASTERIZER_DESC rasterizer;
        D3D10_SAMPLER_DESC sampler;
    } desc;
    /* the sampler's texture, never read. */
    ID3D10ShaderResourceView **texture;
    IUnknown *state;
    UINT assignment_count;
    FxAssignment *assignments;
};

enum { FX_BIND_CB, FX_BIND_TBUFFER, FX_BIND_SRV, FX_BIND_SAMPLER };

typedef struct FxBinding {
    UINT kind, slot, count;
    FxBuffer *buffer;
    FxVariable *variable;
} FxBinding;

struct FxShader {
    UINT kind;
    const BYTE *code;
    UINT length;
    const char *so_decl;
    IUnknown *object;
    /* input signature container for vertex and geometry shaders. */
    ID3D10Blob *signature;
    UINT binding_count;
    FxBinding *bindings;
    /* a global variable, or a private one for inline shaders. */
    FxVariable *variable;
    UINT variable_index;
};

struct FxVariable {
    ID3D10EffectVariable iface;
    FxEffect *effect;
    FxType *type;
    const char *name, *semantic;
    BOOL annotation, member;
    UINT bind_point;
    UINT annotation_count;
    FxVariable *annotations;
    FxBuffer *buffer;
    /* bytes for numerics, the first element's storage for objects. */
    BYTE *data;
    FxVariable *top;
    /* elements, then members, made on first use. */
    FxVariable **children;
};

struct FxBuffer {
    FxVariable var;
    FxType type;
    UINT size;
    BYTE *store;
    BOOL tbuffer, dirty, user;
    ID3D10Buffer *buffer;
    ID3D10ShaderResourceView *view;
    UINT variable_count;
    FxVariable *variables;
};

struct FxPass {
    ID3D10EffectPass iface;
    FxEffect *effect;
    const char *name;
    UINT annotation_count;
    FxVariable *annotations;
    UINT assignment_count;
    FxAssignment *assignments;
    FxBlock *rasterizer, *depth_stencil, *blend;
    ID3D10RenderTargetView **rtv[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
    UINT rtv_count;
    ID3D10DepthStencilView **dsv;
    ID3D10ShaderResourceView **mips;
    FxShader *vs, *ps, *gs;
    UINT stencil_ref, sample_mask;
    FLOAT blend_factor[4];
};

typedef struct FxTechnique {
    ID3D10EffectTechnique iface;
    FxEffect *effect;
    const char *name;
    UINT annotation_count;
    FxVariable *annotations;
    UINT pass_count;
    FxPass *passes;
} FxTechnique;

typedef struct FxArena {
    struct FxArena *next;
} FxArena;

struct FxEffect {
    ID3D10Effect iface;
    ID3D10EffectPool pool_iface;
    LONG refs;
    BOOL is_pool, optimized;
    ID3D10Device *device;
    FxEffect *pool;
    UINT buffer_count, variable_count, technique_count;
    FxBuffer *buffers;
    FxVariable *variables;
    FxTechnique *techniques;
    UINT shader_count, shader_capacity;
    FxShader *shaders;
    UINT block_count, block_capacity;
    FxBlock *blocks;
    FxArena *arena;
};

/* effect.c. */
void *FxAlloc(FxEffect *effect, SIZE_T size);
FxVariable *FxFindVariable(FxEffect *effect, const char *name);
void FxEvaluateBlock(FxEffect *effect, FxBlock *block);

/* effect_expression.c. */
HRESULT FxParseExpression(FxEffect *effect, const BYTE *blob, UINT size, FxExpression **out);
void FxRunExpression(const FxExpression *expression, void *out);

/* effect_variable.c. */
extern FxVariable FxInvalidVariable, FxInvalidBuffer;
extern ID3D10EffectType FxInvalidType;
extern FxVariable FxAnonymousNull[3];
void FxInitType(FxType *type);
void FxInitVariable(FxVariable *variable);
void FxInitBuffer(FxBuffer *buffer);
ID3D10EffectVariable *FxAnnotationByIndex(UINT count, FxVariable *annotations, UINT index);
ID3D10EffectVariable *FxAnnotationByName(UINT count, FxVariable *annotations, LPCSTR name);
UINT FxElementCount(const FxType *type);

#endif
