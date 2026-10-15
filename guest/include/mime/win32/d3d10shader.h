/*
 * direct3d 10 shader reflection: what d3d10.dll's D3D10ReflectShader
 * reads from a compiled shader. the enumerations are direct3d 11's. the
 * interfaces and their method order are windows'.
 */
#ifndef MIME_WIN32_D3D10SHADER_H
#define MIME_WIN32_D3D10SHADER_H

#include <mime/win32/d3d11shader.h>

#define D3D10_SHADER_DEBUG 0x1
#define D3D10_SHADER_SKIP_VALIDATION 0x2
#define D3D10_SHADER_SKIP_OPTIMIZATION 0x4
#define D3D10_SHADER_PACK_MATRIX_ROW_MAJOR 0x8
#define D3D10_SHADER_PACK_MATRIX_COLUMN_MAJOR 0x10
#define D3D10_SHADER_PARTIAL_PRECISION 0x20
#define D3D10_SHADER_FORCE_VS_SOFTWARE_NO_OPT 0x40
#define D3D10_SHADER_FORCE_PS_SOFTWARE_NO_OPT 0x80
#define D3D10_SHADER_NO_PRESHADER 0x100
#define D3D10_SHADER_AVOID_FLOW_CONTROL 0x200
#define D3D10_SHADER_PREFER_FLOW_CONTROL 0x400
#define D3D10_SHADER_ENABLE_STRICTNESS 0x800
#define D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY 0x1000
#define D3D10_SHADER_IEEE_STRICTNESS 0x2000
#define D3D10_SHADER_OPTIMIZATION_LEVEL0 0x4000
#define D3D10_SHADER_OPTIMIZATION_LEVEL1 0x0
#define D3D10_SHADER_OPTIMIZATION_LEVEL2 0xC000
#define D3D10_SHADER_OPTIMIZATION_LEVEL3 0x8000
#define D3D10_SHADER_WARNINGS_ARE_ERRORS 0x40000

typedef D3D_SHADER_VARIABLE_CLASS D3D10_SHADER_VARIABLE_CLASS;
typedef D3D_SHADER_VARIABLE_TYPE D3D10_SHADER_VARIABLE_TYPE;
typedef D3D_SHADER_INPUT_TYPE D3D10_SHADER_INPUT_TYPE;
typedef D3D_CBUFFER_TYPE D3D10_CBUFFER_TYPE;
typedef D3D_RESOURCE_RETURN_TYPE D3D10_RESOURCE_RETURN_TYPE;
typedef D3D_REGISTER_COMPONENT_TYPE D3D10_REGISTER_COMPONENT_TYPE;
typedef D3D_NAME D3D10_NAME;

typedef struct D3D10_SHADER_DESC {
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
} D3D10_SHADER_DESC;

typedef struct D3D10_SHADER_BUFFER_DESC {
    LPCSTR Name;
    D3D10_CBUFFER_TYPE Type;
    UINT Variables;
    UINT Size;
    UINT uFlags;
} D3D10_SHADER_BUFFER_DESC;

typedef struct D3D10_SHADER_VARIABLE_DESC {
    LPCSTR Name;
    UINT StartOffset;
    UINT Size;
    UINT uFlags;
    void *DefaultValue;
} D3D10_SHADER_VARIABLE_DESC;

typedef struct D3D10_SHADER_TYPE_DESC {
    D3D10_SHADER_VARIABLE_CLASS Class;
    D3D10_SHADER_VARIABLE_TYPE Type;
    UINT Rows;
    UINT Columns;
    UINT Elements;
    UINT Members;
    UINT Offset;
} D3D10_SHADER_TYPE_DESC;

typedef struct D3D10_SHADER_INPUT_BIND_DESC {
    LPCSTR Name;
    D3D10_SHADER_INPUT_TYPE Type;
    UINT BindPoint;
    UINT BindCount;
    UINT uFlags;
    D3D10_RESOURCE_RETURN_TYPE ReturnType;
    D3D_SRV_DIMENSION Dimension;
    UINT NumSamples;
} D3D10_SHADER_INPUT_BIND_DESC;

typedef struct D3D10_SIGNATURE_PARAMETER_DESC {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    UINT Register;
    D3D10_NAME SystemValueType;
    D3D10_REGISTER_COMPONENT_TYPE ComponentType;
    BYTE Mask;
    BYTE ReadWriteMask;
} D3D10_SIGNATURE_PARAMETER_DESC;

typedef struct ID3D10ShaderReflectionType ID3D10ShaderReflectionType;
typedef struct ID3D10ShaderReflectionVariable ID3D10ShaderReflectionVariable;
typedef struct ID3D10ShaderReflectionConstantBuffer ID3D10ShaderReflectionConstantBuffer;
typedef struct ID3D10ShaderReflection ID3D10ShaderReflection;

typedef struct ID3D10ShaderReflectionTypeVtbl {
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D10ShaderReflectionType *This, D3D10_SHADER_TYPE_DESC *desc);
    ID3D10ShaderReflectionType *(STDMETHODCALLTYPE *GetMemberTypeByIndex)(ID3D10ShaderReflectionType *This,
                                                                          UINT index);
    ID3D10ShaderReflectionType *(STDMETHODCALLTYPE *GetMemberTypeByName)(ID3D10ShaderReflectionType *This,
                                                                         LPCSTR name);
    LPCSTR(STDMETHODCALLTYPE *GetMemberTypeName)(ID3D10ShaderReflectionType *This, UINT index);
} ID3D10ShaderReflectionTypeVtbl;

struct ID3D10ShaderReflectionType {
    const ID3D10ShaderReflectionTypeVtbl *lpVtbl;
};

typedef struct ID3D10ShaderReflectionVariableVtbl {
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D10ShaderReflectionVariable *This, D3D10_SHADER_VARIABLE_DESC *desc);
    ID3D10ShaderReflectionType *(STDMETHODCALLTYPE *GetType)(ID3D10ShaderReflectionVariable *This);
} ID3D10ShaderReflectionVariableVtbl;

struct ID3D10ShaderReflectionVariable {
    const ID3D10ShaderReflectionVariableVtbl *lpVtbl;
};

typedef struct ID3D10ShaderReflectionConstantBufferVtbl {
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D10ShaderReflectionConstantBuffer *This, D3D10_SHADER_BUFFER_DESC *desc);
    ID3D10ShaderReflectionVariable *(STDMETHODCALLTYPE *GetVariableByIndex)(
        ID3D10ShaderReflectionConstantBuffer *This, UINT index);
    ID3D10ShaderReflectionVariable *(STDMETHODCALLTYPE *GetVariableByName)(
        ID3D10ShaderReflectionConstantBuffer *This, LPCSTR name);
} ID3D10ShaderReflectionConstantBufferVtbl;

struct ID3D10ShaderReflectionConstantBuffer {
    const ID3D10ShaderReflectionConstantBufferVtbl *lpVtbl;
};

typedef struct ID3D10ShaderReflectionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ID3D10ShaderReflection *This, REFIID riid, void **out);
    ULONG(STDMETHODCALLTYPE *AddRef)(ID3D10ShaderReflection *This);
    ULONG(STDMETHODCALLTYPE *Release)(ID3D10ShaderReflection *This);
    HRESULT(STDMETHODCALLTYPE *GetDesc)(ID3D10ShaderReflection *This, D3D10_SHADER_DESC *desc);
    ID3D10ShaderReflectionConstantBuffer *(STDMETHODCALLTYPE *GetConstantBufferByIndex)(ID3D10ShaderReflection *This,
                                                                                        UINT index);
    ID3D10ShaderReflectionConstantBuffer *(STDMETHODCALLTYPE *GetConstantBufferByName)(ID3D10ShaderReflection *This,
                                                                                       LPCSTR name);
    HRESULT(STDMETHODCALLTYPE *GetResourceBindingDesc)(ID3D10ShaderReflection *This, UINT index,
                                                       D3D10_SHADER_INPUT_BIND_DESC *desc);
    HRESULT(STDMETHODCALLTYPE *GetInputParameterDesc)(ID3D10ShaderReflection *This, UINT index,
                                                      D3D10_SIGNATURE_PARAMETER_DESC *desc);
    HRESULT(STDMETHODCALLTYPE *GetOutputParameterDesc)(ID3D10ShaderReflection *This, UINT index,
                                                       D3D10_SIGNATURE_PARAMETER_DESC *desc);
} ID3D10ShaderReflectionVtbl;

struct ID3D10ShaderReflection {
    const ID3D10ShaderReflectionVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_ID3D10ShaderReflection, 0xD40E20B6, 0xF8F7, 0x42AD, 0xAB, 0x20, 0x4B, 0xAF, 0x8F, 0x15, 0xDF,
                 0xAA);

#endif
