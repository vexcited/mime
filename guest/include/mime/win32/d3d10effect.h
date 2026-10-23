/*
 * the direct3d 10 effects framework of d3d10.dll. methods follow windows' vtable order, and only ID3D10Effect
 * and ID3D10EffectPool are reference counted.
 */
#ifndef MIME_WIN32_D3D10EFFECT_H
#define MIME_WIN32_D3D10EFFECT_H

#define D3D10_EFFECT_COMPILE_CHILD_EFFECT 0x1
#define D3D10_EFFECT_COMPILE_ALLOW_SLOW_OPS 0x2
#define D3D10_EFFECT_SINGLE_THREADED 0x8

#define D3D10_EFFECT_VARIABLE_POOLED 0x1
#define D3D10_EFFECT_VARIABLE_ANNOTATION 0x2
#define D3D10_EFFECT_VARIABLE_EXPLICIT_BIND_POINT 0x4

typedef struct D3D10_EFFECT_DESC {
    BOOL IsChildEffect;
    UINT ConstantBuffers;
    UINT SharedConstantBuffers;
    UINT GlobalVariables;
    UINT SharedGlobalVariables;
    UINT Techniques;
} D3D10_EFFECT_DESC;

typedef struct D3D10_EFFECT_VARIABLE_DESC {
    LPCSTR Name;
    LPCSTR Semantic;
    UINT Flags;
    UINT Annotations;
    UINT BufferOffset;
    UINT ExplicitBindPoint;
} D3D10_EFFECT_VARIABLE_DESC;

typedef struct D3D10_EFFECT_TYPE_DESC {
    LPCSTR TypeName;
    D3D10_SHADER_VARIABLE_CLASS Class;
    D3D10_SHADER_VARIABLE_TYPE Type;
    UINT Elements;
    UINT Members;
    UINT Rows;
    UINT Columns;
    UINT PackedSize;
    UINT UnpackedSize;
    UINT Stride;
} D3D10_EFFECT_TYPE_DESC;

typedef struct D3D10_EFFECT_SHADER_DESC {
    const BYTE *pInputSignature;
    BOOL IsInline;
    const BYTE *pBytecode;
    UINT BytecodeLength;
    LPCSTR SODecl;
    UINT NumInputSignatureEntries;
    UINT NumOutputSignatureEntries;
} D3D10_EFFECT_SHADER_DESC;

typedef struct D3D10_TECHNIQUE_DESC {
    LPCSTR Name;
    UINT Passes;
    UINT Annotations;
} D3D10_TECHNIQUE_DESC;

typedef struct D3D10_PASS_DESC {
    LPCSTR Name;
    UINT Annotations;
    BYTE *pIAInputSignature;
    SIZE_T IAInputSignatureSize;
    UINT StencilRef;
    UINT SampleMask;
    FLOAT BlendFactor[4];
} D3D10_PASS_DESC;

typedef struct ID3D10EffectType ID3D10EffectType;
typedef struct ID3D10EffectVariable ID3D10EffectVariable;
typedef struct ID3D10EffectScalarVariable ID3D10EffectScalarVariable;
typedef struct ID3D10EffectVectorVariable ID3D10EffectVectorVariable;
typedef struct ID3D10EffectMatrixVariable ID3D10EffectMatrixVariable;
typedef struct ID3D10EffectStringVariable ID3D10EffectStringVariable;
typedef struct ID3D10EffectShaderResourceVariable ID3D10EffectShaderResourceVariable;
typedef struct ID3D10EffectRenderTargetViewVariable ID3D10EffectRenderTargetViewVariable;
typedef struct ID3D10EffectDepthStencilViewVariable ID3D10EffectDepthStencilViewVariable;
typedef struct ID3D10EffectConstantBuffer ID3D10EffectConstantBuffer;
typedef struct ID3D10EffectShaderVariable ID3D10EffectShaderVariable;
typedef struct ID3D10EffectBlendVariable ID3D10EffectBlendVariable;
typedef struct ID3D10EffectDepthStencilVariable ID3D10EffectDepthStencilVariable;
typedef struct ID3D10EffectRasterizerVariable ID3D10EffectRasterizerVariable;
typedef struct ID3D10EffectSamplerVariable ID3D10EffectSamplerVariable;
typedef struct ID3D10EffectTechnique ID3D10EffectTechnique;
typedef struct ID3D10EffectPass ID3D10EffectPass;

typedef struct D3D10_PASS_SHADER_DESC {
    ID3D10EffectShaderVariable *pShaderVariable;
    UINT ShaderIndex;
} D3D10_PASS_SHADER_DESC;

#define ID3D10EffectType_METHODS(T)                                                                                    \
    BOOL(STDMETHODCALLTYPE *IsValid)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3D10_EFFECT_TYPE_DESC * desc);                                      \
    ID3D10EffectType *(STDMETHODCALLTYPE *GetMemberTypeByIndex)(T * This, UINT index);                                 \
    ID3D10EffectType *(STDMETHODCALLTYPE *GetMemberTypeByName)(T * This, LPCSTR name);                                 \
    ID3D10EffectType *(STDMETHODCALLTYPE *GetMemberTypeBySemantic)(T * This, LPCSTR semantic);                         \
    LPCSTR(STDMETHODCALLTYPE *GetMemberName)(T * This, UINT index);                                                    \
    LPCSTR(STDMETHODCALLTYPE *GetMemberSemantic)(T * This, UINT index);

#define ID3D10EffectVariable_METHODS(T)                                                                                \
    BOOL(STDMETHODCALLTYPE *IsValid)(T * This);                                                                        \
    ID3D10EffectType *(STDMETHODCALLTYPE *GetType)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3D10_EFFECT_VARIABLE_DESC * desc);                                  \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetAnnotationByIndex)(T * This, UINT index);                             \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetAnnotationByName)(T * This, LPCSTR name);                             \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetMemberByIndex)(T * This, UINT index);                                 \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetMemberByName)(T * This, LPCSTR name);                                 \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetMemberBySemantic)(T * This, LPCSTR semantic);                         \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetElement)(T * This, UINT index);                                       \
    ID3D10EffectConstantBuffer *(STDMETHODCALLTYPE *GetParentConstantBuffer)(T * This);                                \
    ID3D10EffectScalarVariable *(STDMETHODCALLTYPE *AsScalar)(T * This);                                               \
    ID3D10EffectVectorVariable *(STDMETHODCALLTYPE *AsVector)(T * This);                                               \
    ID3D10EffectMatrixVariable *(STDMETHODCALLTYPE *AsMatrix)(T * This);                                               \
    ID3D10EffectStringVariable *(STDMETHODCALLTYPE *AsString)(T * This);                                               \
    ID3D10EffectShaderResourceVariable *(STDMETHODCALLTYPE *AsShaderResource)(T * This);                               \
    ID3D10EffectRenderTargetViewVariable *(STDMETHODCALLTYPE *AsRenderTargetView)(T * This);                           \
    ID3D10EffectDepthStencilViewVariable *(STDMETHODCALLTYPE *AsDepthStencilView)(T * This);                           \
    ID3D10EffectConstantBuffer *(STDMETHODCALLTYPE *AsConstantBuffer)(T * This);                                       \
    ID3D10EffectShaderVariable *(STDMETHODCALLTYPE *AsShader)(T * This);                                               \
    ID3D10EffectBlendVariable *(STDMETHODCALLTYPE *AsBlend)(T * This);                                                 \
    ID3D10EffectDepthStencilVariable *(STDMETHODCALLTYPE *AsDepthStencil)(T * This);                                   \
    ID3D10EffectRasterizerVariable *(STDMETHODCALLTYPE *AsRasterizer)(T * This);                                       \
    ID3D10EffectSamplerVariable *(STDMETHODCALLTYPE *AsSampler)(T * This);                                             \
    HRESULT(STDMETHODCALLTYPE *SetRawValue)(T * This, void *data, UINT offset, UINT count);                            \
    HRESULT(STDMETHODCALLTYPE *GetRawValue)(T * This, void *data, UINT offset, UINT count);

#define ID3D10EffectScalarVariable_METHODS(T)                                                                          \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetFloat)(T * This, float value);                       \
    HRESULT(STDMETHODCALLTYPE *GetFloat)(T * This, float *value);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetFloatArray)(T * This, float *data, UINT offset, UINT count);                         \
    HRESULT(STDMETHODCALLTYPE *GetFloatArray)(T * This, float *data, UINT offset, UINT count);                         \
    HRESULT(STDMETHODCALLTYPE *SetInt)(T * This, int value);                                                           \
    HRESULT(STDMETHODCALLTYPE *GetInt)(T * This, int *value);                                                          \
    HRESULT(STDMETHODCALLTYPE *SetIntArray)(T * This, int *data, UINT offset, UINT count);                             \
    HRESULT(STDMETHODCALLTYPE *GetIntArray)(T * This, int *data, UINT offset, UINT count);                             \
    HRESULT(STDMETHODCALLTYPE *SetBool)(T * This, BOOL value);                                                         \
    HRESULT(STDMETHODCALLTYPE *GetBool)(T * This, BOOL * value);                                                       \
    HRESULT(STDMETHODCALLTYPE *SetBoolArray)(T * This, BOOL * data, UINT offset, UINT count);                          \
    HRESULT(STDMETHODCALLTYPE *GetBoolArray)(T * This, BOOL * data, UINT offset, UINT count);

#define ID3D10EffectVectorVariable_METHODS(T)                                                                          \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetBoolVector)(T * This, BOOL * value);                 \
    HRESULT(STDMETHODCALLTYPE *SetIntVector)(T * This, int *value);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetFloatVector)(T * This, float *value);                                                \
    HRESULT(STDMETHODCALLTYPE *GetBoolVector)(T * This, BOOL * value);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetIntVector)(T * This, int *value);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetFloatVector)(T * This, float *value);                                                \
    HRESULT(STDMETHODCALLTYPE *SetBoolVectorArray)(T * This, BOOL * data, UINT offset, UINT count);                    \
    HRESULT(STDMETHODCALLTYPE *SetIntVectorArray)(T * This, int *data, UINT offset, UINT count);                       \
    HRESULT(STDMETHODCALLTYPE *SetFloatVectorArray)(T * This, float *data, UINT offset, UINT count);                   \
    HRESULT(STDMETHODCALLTYPE *GetBoolVectorArray)(T * This, BOOL * data, UINT offset, UINT count);                    \
    HRESULT(STDMETHODCALLTYPE *GetIntVectorArray)(T * This, int *data, UINT offset, UINT count);                       \
    HRESULT(STDMETHODCALLTYPE *GetFloatVectorArray)(T * This, float *data, UINT offset, UINT count);

#define ID3D10EffectMatrixVariable_METHODS(T)                                                                          \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetMatrix)(T * This, float *data);                      \
    HRESULT(STDMETHODCALLTYPE *GetMatrix)(T * This, float *data);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetMatrixArray)(T * This, float *data, UINT offset, UINT count);                        \
    HRESULT(STDMETHODCALLTYPE *GetMatrixArray)(T * This, float *data, UINT offset, UINT count);                        \
    HRESULT(STDMETHODCALLTYPE *SetMatrixTranspose)(T * This, float *data);                                             \
    HRESULT(STDMETHODCALLTYPE *GetMatrixTranspose)(T * This, float *data);                                             \
    HRESULT(STDMETHODCALLTYPE *SetMatrixTransposeArray)(T * This, float *data, UINT offset, UINT count);               \
    HRESULT(STDMETHODCALLTYPE *GetMatrixTransposeArray)(T * This, float *data, UINT offset, UINT count);

#define ID3D10EffectStringVariable_METHODS(T)                                                                          \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *GetString)(T * This, LPCSTR * string);                  \
    HRESULT(STDMETHODCALLTYPE *GetStringArray)(T * This, LPCSTR * strings, UINT offset, UINT count);

#define ID3D10EffectShaderResourceVariable_METHODS(T)                                                                  \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetResource)(T * This,                                  \
                                                                            ID3D10ShaderResourceView * view);          \
    HRESULT(STDMETHODCALLTYPE *GetResource)(T * This, ID3D10ShaderResourceView * *view);                               \
    HRESULT(STDMETHODCALLTYPE *SetResourceArray)(T * This, ID3D10ShaderResourceView * *views, UINT offset,             \
                                                 UINT count);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetResourceArray)(T * This, ID3D10ShaderResourceView * *views, UINT offset,             \
                                                 UINT count);

#define ID3D10EffectRenderTargetViewVariable_METHODS(T)                                                                \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetRenderTarget)(T * This,                              \
                                                                                ID3D10RenderTargetView * view);        \
    HRESULT(STDMETHODCALLTYPE *GetRenderTarget)(T * This, ID3D10RenderTargetView * *view);                             \
    HRESULT(STDMETHODCALLTYPE *SetRenderTargetArray)(T * This, ID3D10RenderTargetView * *views, UINT offset,           \
                                                     UINT count);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetRenderTargetArray)(T * This, ID3D10RenderTargetView * *views, UINT offset,           \
                                                     UINT count);

#define ID3D10EffectDepthStencilViewVariable_METHODS(T)                                                                \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetDepthStencil)(T * This,                              \
                                                                                ID3D10DepthStencilView * view);        \
    HRESULT(STDMETHODCALLTYPE *GetDepthStencil)(T * This, ID3D10DepthStencilView * *view);                             \
    HRESULT(STDMETHODCALLTYPE *SetDepthStencilArray)(T * This, ID3D10DepthStencilView * *views, UINT offset,           \
                                                     UINT count);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetDepthStencilArray)(T * This, ID3D10DepthStencilView * *views, UINT offset,           \
                                                     UINT count);

#define ID3D10EffectConstantBuffer_METHODS(T)                                                                          \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetConstantBuffer)(T * This, ID3D10Buffer * buffer);    \
    HRESULT(STDMETHODCALLTYPE *GetConstantBuffer)(T * This, ID3D10Buffer * *buffer);                                   \
    HRESULT(STDMETHODCALLTYPE *SetTextureBuffer)(T * This, ID3D10ShaderResourceView * view);                           \
    HRESULT(STDMETHODCALLTYPE *GetTextureBuffer)(T * This, ID3D10ShaderResourceView * *view);

#define ID3D10EffectShaderVariable_METHODS(T)                                                                          \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *GetShaderDesc)(T * This, UINT index,                    \
                                                                              D3D10_EFFECT_SHADER_DESC * desc);        \
    HRESULT(STDMETHODCALLTYPE *GetVertexShader)(T * This, UINT index, ID3D10VertexShader * *shader);                   \
    HRESULT(STDMETHODCALLTYPE *GetGeometryShader)(T * This, UINT index, ID3D10GeometryShader * *shader);               \
    HRESULT(STDMETHODCALLTYPE *GetPixelShader)(T * This, UINT index, ID3D10PixelShader * *shader);                     \
    HRESULT(STDMETHODCALLTYPE *GetInputSignatureElementDesc)(T * This, UINT index, UINT element,                       \
                                                             D3D10_SIGNATURE_PARAMETER_DESC * desc);                   \
    HRESULT(STDMETHODCALLTYPE *GetOutputSignatureElementDesc)(T * This, UINT index, UINT element,                      \
                                                              D3D10_SIGNATURE_PARAMETER_DESC * desc);

#define ID3D10EffectBlendVariable_METHODS(T)                                                                           \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *GetBlendState)(T * This, UINT index,                    \
                                                                              ID3D10BlendState * *state);              \
    HRESULT(STDMETHODCALLTYPE *GetBackingStore)(T * This, UINT index, D3D10_BLEND_DESC * desc);

#define ID3D10EffectDepthStencilVariable_METHODS(T)                                                                    \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *GetDepthStencilState)(                                  \
        T * This, UINT index, ID3D10DepthStencilState * *state);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetBackingStore)(T * This, UINT index, D3D10_DEPTH_STENCIL_DESC * desc);

#define ID3D10EffectRasterizerVariable_METHODS(T)                                                                      \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *GetRasterizerState)(T * This, UINT index,               \
                                                                                   ID3D10RasterizerState * *state);    \
    HRESULT(STDMETHODCALLTYPE *GetBackingStore)(T * This, UINT index, D3D10_RASTERIZER_DESC * desc);

#define ID3D10EffectSamplerVariable_METHODS(T)                                                                         \
    ID3D10EffectVariable_METHODS(T) HRESULT(STDMETHODCALLTYPE *GetSampler)(T * This, UINT index,                       \
                                                                           ID3D10SamplerState * *sampler);             \
    HRESULT(STDMETHODCALLTYPE *GetBackingStore)(T * This, UINT index, D3D10_SAMPLER_DESC * desc);

#define ID3D10EffectPass_METHODS(T)                                                                                    \
    BOOL(STDMETHODCALLTYPE *IsValid)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3D10_PASS_DESC * desc);                                             \
    HRESULT(STDMETHODCALLTYPE *GetVertexShaderDesc)(T * This, D3D10_PASS_SHADER_DESC * desc);                          \
    HRESULT(STDMETHODCALLTYPE *GetGeometryShaderDesc)(T * This, D3D10_PASS_SHADER_DESC * desc);                        \
    HRESULT(STDMETHODCALLTYPE *GetPixelShaderDesc)(T * This, D3D10_PASS_SHADER_DESC * desc);                           \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetAnnotationByIndex)(T * This, UINT index);                             \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetAnnotationByName)(T * This, LPCSTR name);                             \
    HRESULT(STDMETHODCALLTYPE *Apply)(T * This, UINT flags);                                                           \
    HRESULT(STDMETHODCALLTYPE *ComputeStateBlockMask)(T * This, D3D10_STATE_BLOCK_MASK * mask);

#define ID3D10EffectTechnique_METHODS(T)                                                                               \
    BOOL(STDMETHODCALLTYPE *IsValid)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3D10_TECHNIQUE_DESC * desc);                                        \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetAnnotationByIndex)(T * This, UINT index);                             \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetAnnotationByName)(T * This, LPCSTR name);                             \
    ID3D10EffectPass *(STDMETHODCALLTYPE *GetPassByIndex)(T * This, UINT index);                                       \
    ID3D10EffectPass *(STDMETHODCALLTYPE *GetPassByName)(T * This, LPCSTR name);                                       \
    HRESULT(STDMETHODCALLTYPE *ComputeStateBlockMask)(T * This, D3D10_STATE_BLOCK_MASK * mask);

#define ID3D10Effect_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(T * This, REFIID riid, void **out);                                     \
    ULONG(STDMETHODCALLTYPE *AddRef)(T * This);                                                                        \
    ULONG(STDMETHODCALLTYPE *Release)(T * This);                                                                       \
    BOOL(STDMETHODCALLTYPE *IsValid)(T * This);                                                                        \
    BOOL(STDMETHODCALLTYPE *IsPool)(T * This);                                                                         \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T * This, ID3D10Device * *device);                                           \
    HRESULT(STDMETHODCALLTYPE *GetDesc)(T * This, D3D10_EFFECT_DESC * desc);                                           \
    ID3D10EffectConstantBuffer *(STDMETHODCALLTYPE *GetConstantBufferByIndex)(T * This, UINT index);                   \
    ID3D10EffectConstantBuffer *(STDMETHODCALLTYPE *GetConstantBufferByName)(T * This, LPCSTR name);                   \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetVariableByIndex)(T * This, UINT index);                               \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetVariableByName)(T * This, LPCSTR name);                               \
    ID3D10EffectVariable *(STDMETHODCALLTYPE *GetVariableBySemantic)(T * This, LPCSTR semantic);                       \
    ID3D10EffectTechnique *(STDMETHODCALLTYPE *GetTechniqueByIndex)(T * This, UINT index);                             \
    ID3D10EffectTechnique *(STDMETHODCALLTYPE *GetTechniqueByName)(T * This, LPCSTR name);                             \
    HRESULT(STDMETHODCALLTYPE *Optimize)(T * This);                                                                    \
    BOOL(STDMETHODCALLTYPE *IsOptimized)(T * This);

#define ID3D10EffectPool_METHODS(T)                                                                                    \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(T * This, REFIID riid, void **out);                                     \
    ULONG(STDMETHODCALLTYPE *AddRef)(T * This);                                                                        \
    ULONG(STDMETHODCALLTYPE *Release)(T * This);                                                                       \
    ID3D10Effect *(STDMETHODCALLTYPE *AsEffect)(T * This);

MIME_COM_INTERFACE(ID3D10EffectType)
MIME_COM_INTERFACE(ID3D10EffectVariable)
MIME_COM_INTERFACE(ID3D10EffectScalarVariable)
MIME_COM_INTERFACE(ID3D10EffectVectorVariable)
MIME_COM_INTERFACE(ID3D10EffectMatrixVariable)
MIME_COM_INTERFACE(ID3D10EffectStringVariable)
MIME_COM_INTERFACE(ID3D10EffectShaderResourceVariable)
MIME_COM_INTERFACE(ID3D10EffectRenderTargetViewVariable)
MIME_COM_INTERFACE(ID3D10EffectDepthStencilViewVariable)
MIME_COM_INTERFACE(ID3D10EffectConstantBuffer)
MIME_COM_INTERFACE(ID3D10EffectShaderVariable)
MIME_COM_INTERFACE(ID3D10EffectBlendVariable)
MIME_COM_INTERFACE(ID3D10EffectDepthStencilVariable)
MIME_COM_INTERFACE(ID3D10EffectRasterizerVariable)
MIME_COM_INTERFACE(ID3D10EffectSamplerVariable)
MIME_COM_INTERFACE(ID3D10EffectPass)
MIME_COM_INTERFACE(ID3D10EffectTechnique)
MIME_COM_INTERFACE(ID3D10Effect)
MIME_COM_INTERFACE(ID3D10EffectPool)

#endif
