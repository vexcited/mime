/*
 * direct3d 11 shader reflection: what d3dcompiler_47.dll's D3DReflect
 * reads from a compiled shader. the interfaces and their method order are
 * windows', as programs call them through the vtables.
 */
#ifndef MIME_WIN32_D3D11SHADER_H
#define MIME_WIN32_D3D11SHADER_H

#include <mime/win32/d3dcommon.h>

typedef enum D3D_SHADER_VARIABLE_CLASS {
    D3D_SVC_SCALAR = 0,
    D3D_SVC_VECTOR = 1,
    D3D_SVC_MATRIX_ROWS = 2,
    D3D_SVC_MATRIX_COLUMNS = 3,
    D3D_SVC_OBJECT = 4,
    D3D_SVC_STRUCT = 5,
    D3D_SVC_INTERFACE_CLASS = 6,
    D3D_SVC_INTERFACE_POINTER = 7
} D3D_SHADER_VARIABLE_CLASS;

typedef enum D3D_SHADER_VARIABLE_FLAGS {
    D3D_SVF_USERPACKED = 1,
    D3D_SVF_USED = 2,
    D3D_SVF_INTERFACE_POINTER = 4,
    D3D_SVF_INTERFACE_PARAMETER = 8
} D3D_SHADER_VARIABLE_FLAGS;

typedef enum D3D_SHADER_VARIABLE_TYPE {
    D3D_SVT_VOID = 0,
    D3D_SVT_BOOL = 1,
    D3D_SVT_INT = 2,
    D3D_SVT_FLOAT = 3,
    D3D_SVT_STRING = 4,
    D3D_SVT_TEXTURE = 5,
    D3D_SVT_TEXTURE1D = 6,
    D3D_SVT_TEXTURE2D = 7,
    D3D_SVT_TEXTURE3D = 8,
    D3D_SVT_TEXTURECUBE = 9,
    D3D_SVT_SAMPLER = 10,
    D3D_SVT_SAMPLER1D = 11,
    D3D_SVT_SAMPLER2D = 12,
    D3D_SVT_SAMPLER3D = 13,
    D3D_SVT_SAMPLERCUBE = 14,
    D3D_SVT_PIXELSHADER = 15,
    D3D_SVT_VERTEXSHADER = 16,
    D3D_SVT_PIXELFRAGMENT = 17,
    D3D_SVT_VERTEXFRAGMENT = 18,
    D3D_SVT_UINT = 19,
    D3D_SVT_UINT8 = 20,
    D3D_SVT_GEOMETRYSHADER = 21,
    D3D_SVT_RASTERIZER = 22,
    D3D_SVT_DEPTHSTENCIL = 23,
    D3D_SVT_BLEND = 24,
    D3D_SVT_BUFFER = 25,
    D3D_SVT_CBUFFER = 26,
    D3D_SVT_TBUFFER = 27,
    D3D_SVT_TEXTURE1DARRAY = 28,
    D3D_SVT_TEXTURE2DARRAY = 29,
    D3D_SVT_RENDERTARGETVIEW = 30,
    D3D_SVT_DEPTHSTENCILVIEW = 31,
    D3D_SVT_TEXTURE2DMS = 32,
    D3D_SVT_TEXTURE2DMSARRAY = 33,
    D3D_SVT_TEXTURECUBEARRAY = 34,
    D3D_SVT_HULLSHADER = 35,
    D3D_SVT_DOMAINSHADER = 36,
    D3D_SVT_INTERFACE_POINTER = 37,
    D3D_SVT_COMPUTESHADER = 38,
    D3D_SVT_DOUBLE = 39,
    D3D_SVT_RWTEXTURE1D = 40,
    D3D_SVT_RWTEXTURE1DARRAY = 41,
    D3D_SVT_RWTEXTURE2D = 42,
    D3D_SVT_RWTEXTURE2DARRAY = 43,
    D3D_SVT_RWTEXTURE3D = 44,
    D3D_SVT_RWBUFFER = 45,
    D3D_SVT_BYTEADDRESS_BUFFER = 46,
    D3D_SVT_RWBYTEADDRESS_BUFFER = 47,
    D3D_SVT_STRUCTURED_BUFFER = 48,
    D3D_SVT_RWSTRUCTURED_BUFFER = 49,
    D3D_SVT_APPEND_STRUCTURED_BUFFER = 50,
    D3D_SVT_CONSUME_STRUCTURED_BUFFER = 51,
    D3D_SVT_MIN8FLOAT = 52,
    D3D_SVT_MIN10FLOAT = 53,
    D3D_SVT_MIN16FLOAT = 54,
    D3D_SVT_MIN12INT = 55,
    D3D_SVT_MIN16INT = 56,
    D3D_SVT_MIN16UINT = 57
} D3D_SHADER_VARIABLE_TYPE;

typedef enum D3D_SHADER_INPUT_FLAGS {
    D3D_SIF_USERPACKED = 0x1,
    D3D_SIF_COMPARISON_SAMPLER = 0x2,
    D3D_SIF_TEXTURE_COMPONENT_0 = 0x4,
    D3D_SIF_TEXTURE_COMPONENT_1 = 0x8,
    D3D_SIF_TEXTURE_COMPONENTS = 0xC,
    D3D_SIF_UNUSED = 0x10
} D3D_SHADER_INPUT_FLAGS;

typedef enum D3D_SHADER_INPUT_TYPE {
    D3D_SIT_CBUFFER = 0,
    D3D_SIT_TBUFFER = 1,
    D3D_SIT_TEXTURE = 2,
    D3D_SIT_SAMPLER = 3,
    D3D_SIT_UAV_RWTYPED = 4,
    D3D_SIT_STRUCTURED = 5,
    D3D_SIT_UAV_RWSTRUCTURED = 6,
    D3D_SIT_BYTEADDRESS = 7,
    D3D_SIT_UAV_RWBYTEADDRESS = 8,
    D3D_SIT_UAV_APPEND_STRUCTURED = 9,
    D3D_SIT_UAV_CONSUME_STRUCTURED = 10,
    D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER = 11
} D3D_SHADER_INPUT_TYPE;

typedef enum D3D_SHADER_CBUFFER_FLAGS {
    D3D_CBF_USERPACKED = 1
} D3D_SHADER_CBUFFER_FLAGS;

typedef enum D3D_CBUFFER_TYPE {
    D3D_CT_CBUFFER = 0,
    D3D_CT_TBUFFER = 1,
    D3D_CT_INTERFACE_POINTERS = 2,
    D3D_CT_RESOURCE_BIND_INFO = 3
} D3D_CBUFFER_TYPE;

typedef enum D3D_RESOURCE_RETURN_TYPE {
    D3D_RETURN_TYPE_UNORM = 1,
    D3D_RETURN_TYPE_SNORM = 2,
    D3D_RETURN_TYPE_SINT = 3,
    D3D_RETURN_TYPE_UINT = 4,
    D3D_RETURN_TYPE_FLOAT = 5,
    D3D_RETURN_TYPE_MIXED = 6,
    D3D_RETURN_TYPE_DOUBLE = 7,
    D3D_RETURN_TYPE_CONTINUED = 8
} D3D_RESOURCE_RETURN_TYPE;

typedef enum D3D_REGISTER_COMPONENT_TYPE {
    D3D_REGISTER_COMPONENT_UNKNOWN = 0,
    D3D_REGISTER_COMPONENT_UINT32 = 1,
    D3D_REGISTER_COMPONENT_SINT32 = 2,
    D3D_REGISTER_COMPONENT_FLOAT32 = 3
} D3D_REGISTER_COMPONENT_TYPE;

typedef enum D3D_NAME {
    D3D_NAME_UNDEFINED = 0,
    D3D_NAME_POSITION = 1,
    D3D_NAME_CLIP_DISTANCE = 2,
    D3D_NAME_CULL_DISTANCE = 3,
    D3D_NAME_RENDER_TARGET_ARRAY_INDEX = 4,
    D3D_NAME_VIEWPORT_ARRAY_INDEX = 5,
    D3D_NAME_VERTEX_ID = 6,
    D3D_NAME_PRIMITIVE_ID = 7,
    D3D_NAME_INSTANCE_ID = 8,
    D3D_NAME_IS_FRONT_FACE = 9,
    D3D_NAME_SAMPLE_INDEX = 10,
    D3D_NAME_FINAL_QUAD_EDGE_TESSFACTOR = 11,
    D3D_NAME_FINAL_QUAD_INSIDE_TESSFACTOR = 12,
    D3D_NAME_FINAL_TRI_EDGE_TESSFACTOR = 13,
    D3D_NAME_FINAL_TRI_INSIDE_TESSFACTOR = 14,
    D3D_NAME_FINAL_LINE_DETAIL_TESSFACTOR = 15,
    D3D_NAME_FINAL_LINE_DENSITY_TESSFACTOR = 16,
    D3D_NAME_TARGET = 64,
    D3D_NAME_DEPTH = 65,
    D3D_NAME_COVERAGE = 66,
    D3D_NAME_DEPTH_GREATER_EQUAL = 67,
    D3D_NAME_DEPTH_LESS_EQUAL = 68,
    D3D_NAME_STENCIL_REF = 69,
    D3D_NAME_INNER_COVERAGE = 70
} D3D_NAME;

typedef enum D3D_MIN_PRECISION {
    D3D_MIN_PRECISION_DEFAULT = 0,
    D3D_MIN_PRECISION_FLOAT_16 = 1,
    D3D_MIN_PRECISION_FLOAT_2_8 = 2,
    D3D_MIN_PRECISION_RESERVED = 3,
    D3D_MIN_PRECISION_SINT_16 = 4,
    D3D_MIN_PRECISION_UINT_16 = 5,
    D3D_MIN_PRECISION_ANY_16 = 0xF0,
    D3D_MIN_PRECISION_ANY_10 = 0xF1
} D3D_MIN_PRECISION;

/* the input primitives of geometry shaders, patches being 8 + control points. */
typedef enum D3D_PRIMITIVE {
    D3D_PRIMITIVE_UNDEFINED = 0,
    D3D_PRIMITIVE_POINT = 1,
    D3D_PRIMITIVE_LINE = 2,
    D3D_PRIMITIVE_TRIANGLE = 3,
    D3D_PRIMITIVE_LINE_ADJ = 6,
    D3D_PRIMITIVE_TRIANGLE_ADJ = 7,
    D3D_PRIMITIVE_1_CONTROL_POINT_PATCH = 8,
    D3D_PRIMITIVE_32_CONTROL_POINT_PATCH = 39
} D3D_PRIMITIVE;

typedef enum D3D_TESSELLATOR_DOMAIN {
    D3D_TESSELLATOR_DOMAIN_UNDEFINED = 0,
    D3D_TESSELLATOR_DOMAIN_ISOLINE = 1,
    D3D_TESSELLATOR_DOMAIN_TRI = 2,
    D3D_TESSELLATOR_DOMAIN_QUAD = 3
} D3D_TESSELLATOR_DOMAIN;

typedef enum D3D_TESSELLATOR_PARTITIONING {
    D3D_TESSELLATOR_PARTITIONING_UNDEFINED = 0,
    D3D_TESSELLATOR_PARTITIONING_INTEGER = 1,
    D3D_TESSELLATOR_PARTITIONING_POW2 = 2,
    D3D_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD = 3,
    D3D_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN = 4
} D3D_TESSELLATOR_PARTITIONING;

typedef enum D3D_TESSELLATOR_OUTPUT_PRIMITIVE {
    D3D_TESSELLATOR_OUTPUT_UNDEFINED = 0,
    D3D_TESSELLATOR_OUTPUT_POINT = 1,
    D3D_TESSELLATOR_OUTPUT_LINE = 2,
    D3D_TESSELLATOR_OUTPUT_TRIANGLE_CW = 3,
    D3D_TESSELLATOR_OUTPUT_TRIANGLE_CCW = 4
} D3D_TESSELLATOR_OUTPUT_PRIMITIVE;

/* GetRequiresFlags. */
#define D3D_SHADER_REQUIRES_DOUBLES 0x00000001
#define D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL 0x00000002
#define D3D_SHADER_REQUIRES_UAVS_AT_EVERY_STAGE 0x00000004
#define D3D_SHADER_REQUIRES_64_UAVS 0x00000008
#define D3D_SHADER_REQUIRES_MINIMUM_PRECISION 0x00000010
#define D3D_SHADER_REQUIRES_11_1_DOUBLE_EXTENSIONS 0x00000020
#define D3D_SHADER_REQUIRES_11_1_SHADER_EXTENSIONS 0x00000040
#define D3D_SHADER_REQUIRES_LEVEL_9_COMPARISON_FILTERING 0x00000080
#define D3D_SHADER_REQUIRES_TILED_RESOURCES 0x00000100

/* the program type of a version, as D3D11_SHADER_DESC holds it. */
#define D3D11_SHVER_PIXEL_SHADER 0
#define D3D11_SHVER_VERTEX_SHADER 1
#define D3D11_SHVER_GEOMETRY_SHADER 2
#define D3D11_SHVER_HULL_SHADER 3
#define D3D11_SHVER_DOMAIN_SHADER 4
#define D3D11_SHVER_COMPUTE_SHADER 5
#define D3D11_SHVER_GET_TYPE(v) (((v) >> 16) & 0xFFFF)
#define D3D11_SHVER_GET_MAJOR(v) (((v) >> 4) & 0xF)
#define D3D11_SHVER_GET_MINOR(v) ((v)&0xF)

typedef struct D3D11_SHADER_DESC {
    UINT Version;
    LPCSTR Creator;
    UINT Flags;
    UINT ConstantBuffers;
    UINT BoundResources;
    UINT InputParameters;
    UINT OutputParameters;
    UINT InstructionCount;
    UINT TempRegisterCount;
    UINT TempArrayCount;
    UINT DefCount;
    UINT DclCount;
    UINT TextureNormalInstructions;
    UINT TextureLoadInstructions;
    UINT TextureCompInstructions;
    UINT TextureBiasInstructions;
    UINT TextureGradientInstructions;
    UINT FloatInstructionCount;
    UINT IntInstructionCount;
    UINT UintInstructionCount;
    UINT StaticFlowControlCount;
    UINT DynamicFlowControlCount;
    UINT MacroInstructionCount;
    UINT ArrayInstructionCount;
    UINT CutInstructionCount;
    UINT EmitInstructionCount;
    D3D_PRIMITIVE_TOPOLOGY GSOutputTopology;
    UINT GSMaxOutputVertexCount;
    D3D_PRIMITIVE InputPrimitive;
    UINT PatchConstantParameters;
    UINT cGSInstanceCount;
    UINT cControlPoints;
    D3D_TESSELLATOR_OUTPUT_PRIMITIVE HSOutputPrimitive;
    D3D_TESSELLATOR_PARTITIONING HSPartitioning;
    D3D_TESSELLATOR_DOMAIN TessellatorDomain;
    UINT cBarrierInstructions;
    UINT cInterlockedInstructions;
    UINT cTextureStoreInstructions;
} D3D11_SHADER_DESC;

typedef struct D3D11_SHADER_BUFFER_DESC {
    LPCSTR Name;
    D3D_CBUFFER_TYPE Type;
    UINT Variables;
    UINT Size;
    UINT uFlags;
} D3D11_SHADER_BUFFER_DESC;

typedef struct D3D11_SHADER_VARIABLE_DESC {
    LPCSTR Name;
    UINT StartOffset;
    UINT Size;
    UINT uFlags;
    LPVOID DefaultValue;
    UINT StartTexture;
    UINT TextureSize;
    UINT StartSampler;
    UINT SamplerSize;
} D3D11_SHADER_VARIABLE_DESC;

typedef struct D3D11_SHADER_TYPE_DESC {
    D3D_SHADER_VARIABLE_CLASS Class;
    D3D_SHADER_VARIABLE_TYPE Type;
    UINT Rows;
    UINT Columns;
    UINT Elements;
    UINT Members;
    UINT Offset;
    LPCSTR Name;
} D3D11_SHADER_TYPE_DESC;

typedef struct D3D11_SHADER_INPUT_BIND_DESC {
    LPCSTR Name;
    D3D_SHADER_INPUT_TYPE Type;
    UINT BindPoint;
    UINT BindCount;
    UINT uFlags;
    D3D_RESOURCE_RETURN_TYPE ReturnType;
    D3D_SRV_DIMENSION Dimension;
    UINT NumSamples;
} D3D11_SHADER_INPUT_BIND_DESC;

typedef struct D3D11_SIGNATURE_PARAMETER_DESC {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    UINT Register;
    D3D_NAME SystemValueType;
    D3D_REGISTER_COMPONENT_TYPE ComponentType;
    BYTE Mask;
    BYTE ReadWriteMask;
    UINT Stream;
    D3D_MIN_PRECISION MinPrecision;
} D3D11_SIGNATURE_PARAMETER_DESC;

typedef struct ID3D11ShaderReflectionType ID3D11ShaderReflectionType;
typedef struct ID3D11ShaderReflectionVariable ID3D11ShaderReflectionVariable;
typedef struct ID3D11ShaderReflectionConstantBuffer ID3D11ShaderReflectionConstantBuffer;
typedef struct ID3D11ShaderReflection ID3D11ShaderReflection;

/* the child objects are not COM objects: they live as long as their reflection. */
typedef struct ID3D11ShaderReflectionTypeVtbl {
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D11ShaderReflectionType *This, D3D11_SHADER_TYPE_DESC *desc);
    ID3D11ShaderReflectionType *(STDMETHODCALLTYPE *GetMemberTypeByIndex)(ID3D11ShaderReflectionType *This,
                                                                         UINT index);
    ID3D11ShaderReflectionType *(STDMETHODCALLTYPE *GetMemberTypeByName)(ID3D11ShaderReflectionType *This,
                                                                        LPCSTR name);
    LPCSTR(STDMETHODCALLTYPE *GetMemberTypeName)(ID3D11ShaderReflectionType *This, UINT index);
    HRESULT(STDMETHODCALLTYPE *IsEqual)(ID3D11ShaderReflectionType *This, ID3D11ShaderReflectionType *type);
    ID3D11ShaderReflectionType *(STDMETHODCALLTYPE *GetSubType)(ID3D11ShaderReflectionType *This);
    ID3D11ShaderReflectionType *(STDMETHODCALLTYPE *GetBaseClass)(ID3D11ShaderReflectionType *This);
    UINT(STDMETHODCALLTYPE *GetNumInterfaces)(ID3D11ShaderReflectionType *This);
    ID3D11ShaderReflectionType *(STDMETHODCALLTYPE *GetInterfaceByIndex)(ID3D11ShaderReflectionType *This,
                                                                        UINT index);
    HRESULT(STDMETHODCALLTYPE *IsOfType)(ID3D11ShaderReflectionType *This, ID3D11ShaderReflectionType *type);
    HRESULT(STDMETHODCALLTYPE *ImplementsInterface)(ID3D11ShaderReflectionType *This,
                                                    ID3D11ShaderReflectionType *base);
} ID3D11ShaderReflectionTypeVtbl;
struct ID3D11ShaderReflectionType {
    const ID3D11ShaderReflectionTypeVtbl *lpVtbl;
};

typedef struct ID3D11ShaderReflectionVariableVtbl {
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D11ShaderReflectionVariable *This, D3D11_SHADER_VARIABLE_DESC *desc);
    ID3D11ShaderReflectionType *(STDMETHODCALLTYPE *GetType)(ID3D11ShaderReflectionVariable *This);
    ID3D11ShaderReflectionConstantBuffer *(STDMETHODCALLTYPE *GetBuffer)(ID3D11ShaderReflectionVariable *This);
    UINT(STDMETHODCALLTYPE *GetInterfaceSlot)(ID3D11ShaderReflectionVariable *This, UINT index);
} ID3D11ShaderReflectionVariableVtbl;
struct ID3D11ShaderReflectionVariable {
    const ID3D11ShaderReflectionVariableVtbl *lpVtbl;
};

typedef struct ID3D11ShaderReflectionConstantBufferVtbl {
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D11ShaderReflectionConstantBuffer *This, D3D11_SHADER_BUFFER_DESC *desc);
    ID3D11ShaderReflectionVariable *(STDMETHODCALLTYPE *GetVariableByIndex)(
        ID3D11ShaderReflectionConstantBuffer *This, UINT index);
    ID3D11ShaderReflectionVariable *(STDMETHODCALLTYPE *GetVariableByName)(
        ID3D11ShaderReflectionConstantBuffer *This, LPCSTR name);
} ID3D11ShaderReflectionConstantBufferVtbl;
struct ID3D11ShaderReflectionConstantBuffer {
    const ID3D11ShaderReflectionConstantBufferVtbl *lpVtbl;
};

typedef struct ID3D11ShaderReflectionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ID3D11ShaderReflection *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(ID3D11ShaderReflection *This);
    ULONG(STDMETHODCALLTYPE *Release)(ID3D11ShaderReflection *This);
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D11ShaderReflection *This, D3D11_SHADER_DESC *desc);
    ID3D11ShaderReflectionConstantBuffer *(STDMETHODCALLTYPE *GetConstantBufferByIndex)(ID3D11ShaderReflection *This,
                                                                                        UINT index);
    ID3D11ShaderReflectionConstantBuffer *(STDMETHODCALLTYPE *GetConstantBufferByName)(ID3D11ShaderReflection *This,
                                                                                       LPCSTR name);
    HRESULT(STDMETHODCALLTYPE *GetResourceBindingDesc)(ID3D11ShaderReflection *This, UINT index,
                                                       D3D11_SHADER_INPUT_BIND_DESC *desc);
    HRESULT(STDMETHODCALLTYPE *GetInputParameterDesc)(ID3D11ShaderReflection *This, UINT index,
                                                      D3D11_SIGNATURE_PARAMETER_DESC *desc);
    HRESULT(STDMETHODCALLTYPE *GetOutputParameterDesc)(ID3D11ShaderReflection *This, UINT index,
                                                       D3D11_SIGNATURE_PARAMETER_DESC *desc);
    HRESULT(STDMETHODCALLTYPE *GetPatchConstantParameterDesc)(ID3D11ShaderReflection *This, UINT index,
                                                              D3D11_SIGNATURE_PARAMETER_DESC *desc);
    ID3D11ShaderReflectionVariable *(STDMETHODCALLTYPE *GetVariableByName)(ID3D11ShaderReflection *This, LPCSTR name);
    HRESULT(STDMETHODCALLTYPE *GetResourceBindingDescByName)(ID3D11ShaderReflection *This, LPCSTR name,
                                                             D3D11_SHADER_INPUT_BIND_DESC *desc);
    UINT(STDMETHODCALLTYPE *GetMovInstructionCount)(ID3D11ShaderReflection *This);
    UINT(STDMETHODCALLTYPE *GetMovcInstructionCount)(ID3D11ShaderReflection *This);
    UINT(STDMETHODCALLTYPE *GetConversionInstructionCount)(ID3D11ShaderReflection *This);
    UINT(STDMETHODCALLTYPE *GetBitwiseInstructionCount)(ID3D11ShaderReflection *This);
    D3D_PRIMITIVE(STDMETHODCALLTYPE *GetGSInputPrimitive)(ID3D11ShaderReflection *This);
    BOOL(STDMETHODCALLTYPE *IsSampleFrequencyShader)(ID3D11ShaderReflection *This);
    UINT(STDMETHODCALLTYPE *GetNumInterfaceSlots)(ID3D11ShaderReflection *This);
    HRESULT(STDMETHODCALLTYPE *GetMinFeatureLevel)(ID3D11ShaderReflection *This, D3D_FEATURE_LEVEL *level);
    UINT(STDMETHODCALLTYPE *GetThreadGroupSize)(ID3D11ShaderReflection *This, UINT *x, UINT *y, UINT *z);
    UINT64(STDMETHODCALLTYPE *GetRequiresFlags)(ID3D11ShaderReflection *This);
} ID3D11ShaderReflectionVtbl;
struct ID3D11ShaderReflection {
    const ID3D11ShaderReflectionVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_ID3D11ShaderReflection, 0x8D536CA1, 0x0CCA, 0x4956, 0xA8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84);
/* the direct3d 12 reflection, which D3DReflect also answers. */
MIME_DEFINE_GUID(IID_ID3D12ShaderReflection, 0x5A58797D, 0xA72C, 0x478D, 0x8B, 0xA2, 0xEF, 0xC6, 0xB0, 0xEF, 0xE8, 0x8E);

#endif
