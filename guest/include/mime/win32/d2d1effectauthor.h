#ifndef MIME_WIN32_D2D1EFFECTAUTHOR_H
#define MIME_WIN32_D2D1EFFECTAUTHOR_H

#include <mime/win32/d2d1.h>
#include <mime/win32/d3d11.h>

typedef HRESULT(CALLBACK *PD2D1_EFFECT_FACTORY)(IUnknown **effect);
typedef HRESULT(CALLBACK *PD2D1_PROPERTY_SET_FUNCTION)(IUnknown *effect, const BYTE *data, UINT32 size);
typedef HRESULT(CALLBACK *PD2D1_PROPERTY_GET_FUNCTION)(const IUnknown *effect, BYTE *data, UINT32 size, UINT32 *actual);

typedef enum D2D1_CHANGE_TYPE {
    D2D1_CHANGE_TYPE_NONE = 0,
    D2D1_CHANGE_TYPE_PROPERTIES = 1,
    D2D1_CHANGE_TYPE_CONTEXT = 2,
    D2D1_CHANGE_TYPE_GRAPH = 3
} D2D1_CHANGE_TYPE;

typedef enum D2D1_PIXEL_OPTIONS { D2D1_PIXEL_OPTIONS_NONE = 0, D2D1_PIXEL_OPTIONS_TRIVIAL_SAMPLING = 1 } D2D1_PIXEL_OPTIONS;

typedef enum D2D1_VERTEX_OPTIONS {
    D2D1_VERTEX_OPTIONS_NONE = 0,
    D2D1_VERTEX_OPTIONS_DO_NOT_CLEAR = 1,
    D2D1_VERTEX_OPTIONS_USE_DEPTH_BUFFER = 2,
    D2D1_VERTEX_OPTIONS_ASSUME_NO_OVERLAP = 4
} D2D1_VERTEX_OPTIONS;

typedef enum D2D1_VERTEX_USAGE { D2D1_VERTEX_USAGE_STATIC = 0, D2D1_VERTEX_USAGE_DYNAMIC = 1 } D2D1_VERTEX_USAGE;

typedef enum D2D1_BLEND_OPERATION {
    D2D1_BLEND_OPERATION_ADD = 1,
    D2D1_BLEND_OPERATION_SUBTRACT = 2,
    D2D1_BLEND_OPERATION_REV_SUBTRACT = 3,
    D2D1_BLEND_OPERATION_MIN = 4,
    D2D1_BLEND_OPERATION_MAX = 5
} D2D1_BLEND_OPERATION;

typedef enum D2D1_BLEND {
    D2D1_BLEND_ZERO = 1,
    D2D1_BLEND_ONE = 2,
    D2D1_BLEND_SRC_COLOR = 3,
    D2D1_BLEND_INV_SRC_COLOR = 4,
    D2D1_BLEND_SRC_ALPHA = 5,
    D2D1_BLEND_INV_SRC_ALPHA = 6,
    D2D1_BLEND_DEST_ALPHA = 7,
    D2D1_BLEND_INV_DEST_ALPHA = 8,
    D2D1_BLEND_DEST_COLOR = 9,
    D2D1_BLEND_INV_DEST_COLOR = 10,
    D2D1_BLEND_SRC_ALPHA_SAT = 11,
    D2D1_BLEND_BLEND_FACTOR = 14,
    D2D1_BLEND_INV_BLEND_FACTOR = 15
} D2D1_BLEND;

typedef enum D2D1_CHANNEL_DEPTH { D2D1_CHANNEL_DEPTH_DEFAULT = 0, D2D1_CHANNEL_DEPTH_1 = 1, D2D1_CHANNEL_DEPTH_4 = 4 } D2D1_CHANNEL_DEPTH;

/* the values of D3D11_FILTER. */
typedef enum D2D1_FILTER {
    D2D1_FILTER_MIN_MAG_MIP_POINT = 0x00,
    D2D1_FILTER_MIN_MAG_POINT_MIP_LINEAR = 0x01,
    D2D1_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x04,
    D2D1_FILTER_MIN_POINT_MAG_MIP_LINEAR = 0x05,
    D2D1_FILTER_MIN_LINEAR_MAG_MIP_POINT = 0x10,
    D2D1_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x11,
    D2D1_FILTER_MIN_MAG_LINEAR_MIP_POINT = 0x14,
    D2D1_FILTER_MIN_MAG_MIP_LINEAR = 0x15,
    D2D1_FILTER_ANISOTROPIC = 0x55
} D2D1_FILTER;

typedef enum D2D1_FEATURE { D2D1_FEATURE_DOUBLES = 0, D2D1_FEATURE_D3D10_X_HARDWARE_OPTIONS = 1 } D2D1_FEATURE;

typedef struct D2D1_PROPERTY_BINDING {
    const WCHAR *propertyName;
    PD2D1_PROPERTY_SET_FUNCTION setFunction;
    PD2D1_PROPERTY_GET_FUNCTION getFunction;
} D2D1_PROPERTY_BINDING;

typedef struct D2D1_RESOURCE_TEXTURE_PROPERTIES {
    const UINT32 *extents;
    UINT32 dimensions;
    D2D1_BUFFER_PRECISION bufferPrecision;
    D2D1_CHANNEL_DEPTH channelDepth;
    D2D1_FILTER filter;
    const D2D1_EXTEND_MODE *extendModes;
} D2D1_RESOURCE_TEXTURE_PROPERTIES;

typedef struct D2D1_INPUT_ELEMENT_DESC {
    const char *semanticName;
    UINT32 semanticIndex;
    DXGI_FORMAT format;
    UINT32 inputSlot;
    UINT32 alignedByteOffset;
} D2D1_INPUT_ELEMENT_DESC;

typedef struct D2D1_VERTEX_BUFFER_PROPERTIES {
    UINT32 inputCount;
    D2D1_VERTEX_USAGE usage;
    const BYTE *data;
    UINT32 byteWidth;
} D2D1_VERTEX_BUFFER_PROPERTIES;

typedef struct D2D1_CUSTOM_VERTEX_BUFFER_PROPERTIES {
    const BYTE *shaderBufferWithInputSignature;
    UINT32 shaderBufferSize;
    const D2D1_INPUT_ELEMENT_DESC *inputElements;
    UINT32 elementCount;
    UINT32 stride;
} D2D1_CUSTOM_VERTEX_BUFFER_PROPERTIES;

typedef struct D2D1_VERTEX_RANGE {
    UINT32 startVertex;
    UINT32 vertexCount;
} D2D1_VERTEX_RANGE;

typedef struct D2D1_BLEND_DESCRIPTION {
    D2D1_BLEND sourceBlend;
    D2D1_BLEND destinationBlend;
    D2D1_BLEND_OPERATION blendOperation;
    D2D1_BLEND sourceBlendAlpha;
    D2D1_BLEND destinationBlendAlpha;
    D2D1_BLEND_OPERATION blendOperationAlpha;
    FLOAT blendFactor[4];
} D2D1_BLEND_DESCRIPTION;

typedef struct D2D1_INPUT_DESCRIPTION {
    D2D1_FILTER filter;
    UINT32 levelOfDetailCount;
} D2D1_INPUT_DESCRIPTION;

typedef struct D2D1_FEATURE_DATA_DOUBLES {
    BOOL doublePrecisionFloatShaderOps;
} D2D1_FEATURE_DATA_DOUBLES;

typedef struct D2D1_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS {
    BOOL computeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x;
} D2D1_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS;

typedef struct ID2D1VertexBuffer ID2D1VertexBuffer;
typedef struct ID2D1ResourceTexture ID2D1ResourceTexture;
typedef struct ID2D1RenderInfo ID2D1RenderInfo;
typedef struct ID2D1DrawInfo ID2D1DrawInfo;
typedef struct ID2D1ComputeInfo ID2D1ComputeInfo;
typedef struct ID2D1TransformNode ID2D1TransformNode;
typedef struct ID2D1TransformGraph ID2D1TransformGraph;
typedef struct ID2D1Transform ID2D1Transform;
typedef struct ID2D1DrawTransform ID2D1DrawTransform;
typedef struct ID2D1ComputeTransform ID2D1ComputeTransform;
typedef struct ID2D1AnalysisTransform ID2D1AnalysisTransform;
typedef struct ID2D1SourceTransform ID2D1SourceTransform;
typedef struct ID2D1ConcreteTransform ID2D1ConcreteTransform;
typedef struct ID2D1BlendTransform ID2D1BlendTransform;
typedef struct ID2D1BorderTransform ID2D1BorderTransform;
typedef struct ID2D1OffsetTransform ID2D1OffsetTransform;
typedef struct ID2D1BoundsAdjustmentTransform ID2D1BoundsAdjustmentTransform;
typedef struct ID2D1EffectImpl ID2D1EffectImpl;
typedef struct ID2D1EffectContext ID2D1EffectContext;
typedef struct ID2D1EffectContext1 ID2D1EffectContext1;
typedef struct ID2D1EffectContext2 ID2D1EffectContext2;

#define ID2D1VertexBuffer_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Map)(T * This, BYTE * *data, UINT32 size);                                              \
    HRESULT(STDMETHODCALLTYPE *Unmap)(T * This);
MIME_COM_INTERFACE(ID2D1VertexBuffer)

#define ID2D1ResourceTexture_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Update)(T * This, const UINT32 *minimum, const UINT32 *maximum, const UINT32 *strides,  \
                                       UINT32 dimensions, const BYTE *data, UINT32 count);
MIME_COM_INTERFACE(ID2D1ResourceTexture)

#define ID2D1RenderInfo_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetInputDescription)(T * This, UINT32 index, D2D1_INPUT_DESCRIPTION description);       \
    HRESULT(STDMETHODCALLTYPE *SetOutputBuffer)(T * This, D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth);  \
    void(STDMETHODCALLTYPE *SetCached)(T * This, BOOL cached);                                                         \
    void(STDMETHODCALLTYPE *SetInstructionCountHint)(T * This, UINT32 count);
MIME_COM_INTERFACE(ID2D1RenderInfo)

#define ID2D1DrawInfo_METHODS(T)                                                                                       \
    ID2D1RenderInfo_METHODS(T)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *SetPixelShaderConstantBuffer)(T * This, const BYTE *buffer, UINT32 count);              \
    HRESULT(STDMETHODCALLTYPE *SetResourceTexture)(T * This, UINT32 index, ID2D1ResourceTexture * texture);            \
    HRESULT(STDMETHODCALLTYPE *SetVertexShaderConstantBuffer)(T * This, const BYTE *buffer, UINT32 count);             \
    HRESULT(STDMETHODCALLTYPE *SetPixelShader)(T * This, REFGUID id, D2D1_PIXEL_OPTIONS options);                      \
    HRESULT(STDMETHODCALLTYPE *SetVertexProcessing)(T * This, ID2D1VertexBuffer * buffer, D2D1_VERTEX_OPTIONS options, \
                                                    const D2D1_BLEND_DESCRIPTION *blend,                               \
                                                    const D2D1_VERTEX_RANGE *range, const GUID *shader);
MIME_COM_INTERFACE(ID2D1DrawInfo)

#define ID2D1ComputeInfo_METHODS(T)                                                                                    \
    ID2D1RenderInfo_METHODS(T)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *SetComputeShaderConstantBuffer)(T * This, const BYTE *buffer, UINT32 count);            \
    HRESULT(STDMETHODCALLTYPE *SetComputeShader)(T * This, REFGUID id);                                                \
    HRESULT(STDMETHODCALLTYPE *SetResourceTexture)(T * This, UINT32 index, ID2D1ResourceTexture * texture);
MIME_COM_INTERFACE(ID2D1ComputeInfo)

#define ID2D1TransformNode_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    UINT32(STDMETHODCALLTYPE *GetInputCount)(T * This);
MIME_COM_INTERFACE(ID2D1TransformNode)

#define ID2D1TransformGraph_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    UINT32(STDMETHODCALLTYPE *GetInputCount)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *SetSingleTransformNode)(T * This, ID2D1TransformNode * node);                           \
    HRESULT(STDMETHODCALLTYPE *AddNode)(T * This, ID2D1TransformNode * node);                                          \
    HRESULT(STDMETHODCALLTYPE *RemoveNode)(T * This, ID2D1TransformNode * node);                                       \
    HRESULT(STDMETHODCALLTYPE *SetOutputNode)(T * This, ID2D1TransformNode * node);                                    \
    HRESULT(STDMETHODCALLTYPE *ConnectNode)(T * This, ID2D1TransformNode * from, ID2D1TransformNode * to,              \
                                            UINT32 to_input);                                                          \
    HRESULT(STDMETHODCALLTYPE *ConnectToEffectInput)(T * This, UINT32 effect_input, ID2D1TransformNode * node,         \
                                                     UINT32 node_input);                                               \
    void(STDMETHODCALLTYPE *Clear)(T * This);                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetPassthroughGraph)(T * This, UINT32 effect_input);
MIME_COM_INTERFACE(ID2D1TransformGraph)

#define ID2D1Transform_METHODS(T)                                                                                      \
    ID2D1TransformNode_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *MapOutputRectToInputRects)(T * This, const D2D1_RECT_L *output, D2D1_RECT_L *inputs,    \
                                                          UINT32 count);                                               \
    HRESULT(STDMETHODCALLTYPE *MapInputRectsToOutputRect)(T * This, const D2D1_RECT_L *inputs,                         \
                                                          const D2D1_RECT_L *opaque_inputs, UINT32 count,              \
                                                          D2D1_RECT_L *output, D2D1_RECT_L *opaque_output);            \
    HRESULT(STDMETHODCALLTYPE *MapInvalidRect)(T * This, UINT32 index, D2D1_RECT_L invalid_input,                      \
                                               D2D1_RECT_L * invalid_output);
MIME_COM_INTERFACE(ID2D1Transform)

#define ID2D1DrawTransform_METHODS(T)                                                                                  \
    ID2D1Transform_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetDrawInfo)(T * This, ID2D1DrawInfo * info);
MIME_COM_INTERFACE(ID2D1DrawTransform)

#define ID2D1ComputeTransform_METHODS(T)                                                                               \
    ID2D1Transform_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetComputeInfo)(T * This, ID2D1ComputeInfo * info);                                     \
    HRESULT(STDMETHODCALLTYPE *CalculateThreadgroups)(T * This, const D2D1_RECT_L *output, UINT32 *x, UINT32 *y,       \
                                                      UINT32 *z);
MIME_COM_INTERFACE(ID2D1ComputeTransform)

#define ID2D1AnalysisTransform_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ProcessAnalysisResults)(T * This, const BYTE *data, UINT32 count);
MIME_COM_INTERFACE(ID2D1AnalysisTransform)

#define ID2D1SourceTransform_METHODS(T)                                                                                \
    ID2D1Transform_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *SetRenderInfo)(T * This, ID2D1RenderInfo * info);                                       \
    HRESULT(STDMETHODCALLTYPE *Draw)(T * This, ID2D1Bitmap1 * target, const D2D1_RECT_L *rect,                        \
                                     D2D1_POINT_2U origin);
MIME_COM_INTERFACE(ID2D1SourceTransform)

#define ID2D1ConcreteTransform_METHODS(T)                                                                              \
    ID2D1TransformNode_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *SetOutputBuffer)(T * This, D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth);  \
    void(STDMETHODCALLTYPE *SetCached)(T * This, BOOL cached);
MIME_COM_INTERFACE(ID2D1ConcreteTransform)

#define ID2D1BlendTransform_METHODS(T)                                                                                 \
    ID2D1ConcreteTransform_METHODS(T)                                                                                  \
    void(STDMETHODCALLTYPE *SetDescription)(T * This, const D2D1_BLEND_DESCRIPTION *description);                      \
    void(STDMETHODCALLTYPE *GetDescription)(T * This, D2D1_BLEND_DESCRIPTION * description);
MIME_COM_INTERFACE(ID2D1BlendTransform)

#define ID2D1BorderTransform_METHODS(T)                                                                                \
    ID2D1ConcreteTransform_METHODS(T)                                                                                  \
    void(STDMETHODCALLTYPE *SetExtendModeX)(T * This, D2D1_EXTEND_MODE mode);                                          \
    void(STDMETHODCALLTYPE *SetExtendModeY)(T * This, D2D1_EXTEND_MODE mode);                                          \
    D2D1_EXTEND_MODE(STDMETHODCALLTYPE *GetExtendModeX)(T * This);                                                     \
    D2D1_EXTEND_MODE(STDMETHODCALLTYPE *GetExtendModeY)(T * This);
MIME_COM_INTERFACE(ID2D1BorderTransform)

/* GetOffset returns a struct, which msvc's member functions give through a pointer after the object. */
#define ID2D1OffsetTransform_METHODS(T)                                                                                \
    ID2D1TransformNode_METHODS(T)                                                                                      \
    void(STDMETHODCALLTYPE *SetOffset)(T * This, D2D1_POINT_2L offset);                                                \
    D2D1_POINT_2L *(STDMETHODCALLTYPE *GetOffset)(T * This, D2D1_POINT_2L * ret);
MIME_COM_INTERFACE(ID2D1OffsetTransform)

#define ID2D1BoundsAdjustmentTransform_METHODS(T)                                                                      \
    ID2D1TransformNode_METHODS(T)                                                                                      \
    void(STDMETHODCALLTYPE *SetOutputBounds)(T * This, const D2D1_RECT_L *bounds);                                     \
    void(STDMETHODCALLTYPE *GetOutputBounds)(T * This, D2D1_RECT_L * bounds);
MIME_COM_INTERFACE(ID2D1BoundsAdjustmentTransform)

#define ID2D1EffectImpl_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, ID2D1EffectContext * context, ID2D1TransformGraph * graph);       \
    HRESULT(STDMETHODCALLTYPE *PrepareForRender)(T * This, D2D1_CHANGE_TYPE change);                                   \
    HRESULT(STDMETHODCALLTYPE *SetGraph)(T * This, ID2D1TransformGraph * graph);
MIME_COM_INTERFACE(ID2D1EffectImpl)

#define ID2D1EffectContext_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetDpi)(T * This, FLOAT * dpi_x, FLOAT * dpi_y);                                           \
    HRESULT(STDMETHODCALLTYPE *CreateEffect)(T * This, REFCLSID clsid, ID2D1Effect * *effect);                         \
    HRESULT(STDMETHODCALLTYPE *GetMaximumSupportedFeatureLevel)(T * This, const D3D_FEATURE_LEVEL *levels,             \
                                                                UINT32 count, D3D_FEATURE_LEVEL *level);               \
    HRESULT(STDMETHODCALLTYPE *CreateTransformNodeFromEffect)(T * This, ID2D1Effect * effect,                          \
                                                              ID2D1TransformNode * *node);                             \
    HRESULT(STDMETHODCALLTYPE *CreateBlendTransform)(T * This, UINT32 inputs, const D2D1_BLEND_DESCRIPTION *blend,     \
                                                     ID2D1BlendTransform **transform);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateBorderTransform)(T * This, D2D1_EXTEND_MODE x, D2D1_EXTEND_MODE y,                \
                                                      ID2D1BorderTransform **transform);                               \
    HRESULT(STDMETHODCALLTYPE *CreateOffsetTransform)(T * This, D2D1_POINT_2L offset,                                  \
                                                      ID2D1OffsetTransform **transform);                               \
    HRESULT(STDMETHODCALLTYPE *CreateBoundsAdjustmentTransform)(T * This, const D2D1_RECT_L *rect,                     \
                                                                ID2D1BoundsAdjustmentTransform **transform);           \
    HRESULT(STDMETHODCALLTYPE *LoadPixelShader)(T * This, REFGUID id, const BYTE *code, UINT32 size);                  \
    HRESULT(STDMETHODCALLTYPE *LoadVertexShader)(T * This, REFGUID id, const BYTE *code, UINT32 size);                 \
    HRESULT(STDMETHODCALLTYPE *LoadComputeShader)(T * This, REFGUID id, const BYTE *code, UINT32 size);                \
    BOOL(STDMETHODCALLTYPE *IsShaderLoaded)(T * This, REFGUID id);                                                     \
    HRESULT(STDMETHODCALLTYPE *CreateResourceTexture)(T * This, const GUID *id,                                        \
                                                      const D2D1_RESOURCE_TEXTURE_PROPERTIES *props,                   \
                                                      const BYTE *data, const UINT32 *strides, UINT32 size,            \
                                                      ID2D1ResourceTexture **texture);                                 \
    HRESULT(STDMETHODCALLTYPE *FindResourceTexture)(T * This, const GUID *id, ID2D1ResourceTexture **texture);         \
    HRESULT(STDMETHODCALLTYPE *CreateVertexBuffer)(T * This, const D2D1_VERTEX_BUFFER_PROPERTIES *props,               \
                                                   const GUID *id,                                                     \
                                                   const D2D1_CUSTOM_VERTEX_BUFFER_PROPERTIES *custom,                 \
                                                   ID2D1VertexBuffer **buffer);                                        \
    HRESULT(STDMETHODCALLTYPE *FindVertexBuffer)(T * This, const GUID *id, ID2D1VertexBuffer **buffer);                \
    HRESULT(STDMETHODCALLTYPE *CreateColorContext)(T * This, D2D1_COLOR_SPACE space, const BYTE *profile,              \
                                                   UINT32 size, ID2D1ColorContext **context);                          \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromFilename)(T * This, const WCHAR *filename,                        \
                                                               ID2D1ColorContext **context);                           \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromWicColorContext)(T * This, IWICColorContext * wic,                \
                                                                      ID2D1ColorContext **context);                    \
    HRESULT(STDMETHODCALLTYPE *CheckFeatureSupport)(T * This, D2D1_FEATURE feature, void *data, UINT32 size);          \
    BOOL(STDMETHODCALLTYPE *IsBufferPrecisionSupported)(T * This, D2D1_BUFFER_PRECISION precision);
MIME_COM_INTERFACE(ID2D1EffectContext)

#define ID2D1EffectContext1_METHODS(T)                                                                                 \
    ID2D1EffectContext_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *CreateLookupTable3D)(T * This, D2D1_BUFFER_PRECISION precision, const UINT32 *extents,  \
                                                    const BYTE *data, UINT32 count, const UINT32 *strides,             \
                                                    ID2D1LookupTable3D **table);
MIME_COM_INTERFACE(ID2D1EffectContext1)

#define ID2D1EffectContext2_METHODS(T)                                                                                 \
    ID2D1EffectContext1_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromDxgiColorSpace)(T * This, DXGI_COLOR_SPACE_TYPE space,            \
                                                                     ID2D1ColorContext1 **context);                    \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromSimpleColorProfile)(T * This, const void *profile,                \
                                                                         ID2D1ColorContext1 **context);
MIME_COM_INTERFACE(ID2D1EffectContext2)

MIME_DEFINE_GUID(IID_ID2D1ComputeInfo, 0x5598b14b, 0x9fd7, 0x48b7, 0x9b, 0xdb, 0x8f, 0x09, 0x64, 0xeb, 0x38, 0xbc);
MIME_DEFINE_GUID(IID_ID2D1ComputeTransform, 0x0d85573c, 0x01e3, 0x4f7d, 0xbf, 0xd9, 0x0d, 0x60, 0x60, 0x8b, 0xf3, 0xc3);
MIME_DEFINE_GUID(IID_ID2D1AnalysisTransform, 0x0359dc30, 0x95e6, 0x4568, 0x90, 0x55, 0x27, 0x72, 0x0d, 0x13, 0x0e, 0x93);
MIME_DEFINE_GUID(IID_ID2D1SourceTransform, 0xdb1800dd, 0x0c34, 0x4cf9, 0xbe, 0x90, 0x31, 0xcc, 0x0a, 0x56, 0x53, 0xe1);
MIME_DEFINE_GUID(IID_ID2D1EffectContext1, 0x84ab595a, 0xfc81, 0x4546, 0xba, 0xcd, 0xe8, 0xef, 0x4d, 0x8a, 0xbe, 0x7a);
MIME_DEFINE_GUID(IID_ID2D1EffectContext2, 0x577ad2a0, 0x9fc7, 0x4dda, 0x8b, 0x18, 0xda, 0xb8, 0x10, 0x14, 0x00, 0x52);

#endif
