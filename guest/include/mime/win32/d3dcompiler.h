/*
 * d3dcompiler_47.dll: compiling, reflecting, disassembling and editing
 * direct3d shaders and the dxbc containers that hold them.
 */
#ifndef MIME_WIN32_D3DCOMPILER_H
#define MIME_WIN32_D3DCOMPILER_H

#include <mime/win32/d3dcommon.h>
#include <mime/win32/d3d11shader.h>

#ifndef MIME_BUILD_D3DCOMPILER
#define D3DCOMPILERAPI DECLSPEC_IMPORT
#else
#define D3DCOMPILERAPI DECLSPEC_EXPORT
#endif

#define D3DCOMPILER_DLL_W L"d3dcompiler_47.dll"
#define D3DCOMPILER_DLL_A "d3dcompiler_47.dll"

/* the d3d9 style error of invalid calls, which the container functions return. */
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086C)

typedef enum D3D_BLOB_PART {
    D3D_BLOB_INPUT_SIGNATURE_BLOB = 0,
    D3D_BLOB_OUTPUT_SIGNATURE_BLOB = 1,
    D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB = 2,
    D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB = 3,
    D3D_BLOB_ALL_SIGNATURE_BLOB = 4,
    D3D_BLOB_DEBUG_INFO = 5,
    D3D_BLOB_LEGACY_SHADER = 6,
    D3D_BLOB_XNA_PREPASS_SHADER = 7,
    D3D_BLOB_XNA_SHADER = 8,
    D3D_BLOB_PDB = 9,
    D3D_BLOB_PRIVATE_DATA = 10,
    D3D_BLOB_ROOT_SIGNATURE = 11,
    D3D_BLOB_DEBUG_NAME = 12,
    D3D_BLOB_TEST_ALTERNATE_SHADER = 0x8000,
    D3D_BLOB_TEST_COMPILE_DETAILS = 0x8001,
    D3D_BLOB_TEST_COMPILE_PERF = 0x8002,
    D3D_BLOB_TEST_COMPILE_REPORT = 0x8003
} D3D_BLOB_PART;

typedef enum D3DCOMPILER_STRIP_FLAGS {
    D3DCOMPILER_STRIP_REFLECTION_DATA = 0x1,
    D3DCOMPILER_STRIP_DEBUG_INFO = 0x2,
    D3DCOMPILER_STRIP_TEST_BLOBS = 0x4,
    D3DCOMPILER_STRIP_PRIVATE_DATA = 0x8,
    D3DCOMPILER_STRIP_ROOT_SIGNATURE = 0x10
} D3DCOMPILER_STRIP_FLAGS;

#define D3D_DISASM_ENABLE_COLOR_CODE 0x1
#define D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS 0x2
#define D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING 0x4
#define D3D_DISASM_ENABLE_INSTRUCTION_CYCLE 0x8
#define D3D_DISASM_DISABLE_DEBUG_INFO 0x10
#define D3D_DISASM_ENABLE_INSTRUCTION_OFFSET 0x20
#define D3D_DISASM_INSTRUCTION_ONLY 0x40
#define D3D_DISASM_PRINT_HEX_LITERALS 0x80

#define D3D_GET_INST_OFFSETS_INCLUDE_NON_EXECUTABLE 0x1

#define D3D_COMPRESS_SHADER_KEEP_ALL_PARTS 0x1

typedef struct D3D_SHADER_DATA {
    LPCVOID pBytecode;
    SIZE_T BytecodeLength;
} D3D_SHADER_DATA;

typedef enum D3D_INCLUDE_TYPE {
    D3D_INCLUDE_LOCAL = 0,
    D3D_INCLUDE_SYSTEM = 1
} D3D_INCLUDE_TYPE;

/* the files #include opens, as the program provides them. */
typedef struct ID3DInclude ID3DInclude;
typedef struct ID3DIncludeVtbl {
    HRESULT(STDMETHODCALLTYPE *Open)(ID3DInclude *This, D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent,
                                     LPCVOID *data, UINT *bytes);
    HRESULT(STDMETHODCALLTYPE *Close)(ID3DInclude *This, LPCVOID data);
} ID3DIncludeVtbl;
struct ID3DInclude {
    const ID3DIncludeVtbl *lpVtbl;
};
#define D3D_COMPILE_STANDARD_FILE_INCLUDE ((ID3DInclude *)(UINT_PTR)1)

typedef struct ID3D11Module ID3D11Module;
typedef struct ID3D11Linker ID3D11Linker;
typedef struct ID3D11FunctionLinkingGraph ID3D11FunctionLinkingGraph;
typedef struct ID3D11ShaderTrace ID3D11ShaderTrace;
typedef struct ID3D10Effect ID3D10Effect;

D3DCOMPILERAPI HRESULT WINAPI D3DCreateBlob(SIZE_T size, ID3DBlob **blob);
D3DCOMPILERAPI HRESULT WINAPI D3DReadFileToBlob(LPCWSTR name, ID3DBlob **contents);
D3DCOMPILERAPI HRESULT WINAPI D3DWriteBlobToFile(ID3DBlob *blob, LPCWSTR name, BOOL overwrite);

D3DCOMPILERAPI HRESULT WINAPI D3DGetBlobPart(LPCVOID data, SIZE_T size, D3D_BLOB_PART part, UINT flags,
                                             ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DSetBlobPart(LPCVOID data, SIZE_T size, D3D_BLOB_PART part, UINT flags,
                                             LPCVOID part_data, SIZE_T part_size, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DGetInputSignatureBlob(LPCVOID data, SIZE_T size, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DGetOutputSignatureBlob(LPCVOID data, SIZE_T size, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DGetInputAndOutputSignatureBlob(LPCVOID data, SIZE_T size, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DGetDebugInfo(LPCVOID data, SIZE_T size, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DStripShader(LPCVOID data, SIZE_T size, UINT flags, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DCompressShaders(UINT count, D3D_SHADER_DATA *shaders, UINT flags,
                                                 ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DDecompressShaders(LPCVOID data, SIZE_T size, UINT count, UINT start,
                                                   UINT *indices, UINT flags, ID3DBlob **shaders, UINT *total);

D3DCOMPILERAPI HRESULT WINAPI D3DReflect(LPCVOID data, SIZE_T size, REFIID riid, void **reflector);
D3DCOMPILERAPI HRESULT WINAPI D3DReflectLibrary(LPCVOID data, SIZE_T size, REFIID riid, void **reflector);

D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble(LPCVOID data, SIZE_T size, UINT flags, LPCSTR comments,
                                             ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DDisassembleRegion(LPCVOID data, SIZE_T size, UINT flags, LPCSTR comments,
                                                   SIZE_T start, SIZE_T count, SIZE_T *finish, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble10Effect(ID3D10Effect *effect, UINT flags, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble11Trace(LPCVOID data, SIZE_T size, ID3D11ShaderTrace *trace,
                                                    UINT start, UINT count, UINT flags, ID3DBlob **out);
D3DCOMPILERAPI HRESULT WINAPI D3DGetTraceInstructionOffsets(LPCVOID data, SIZE_T size, UINT flags, SIZE_T start,
                                                            SIZE_T count, SIZE_T *offsets, SIZE_T *total);

D3DCOMPILERAPI HRESULT WINAPI D3DCompile(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                         ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                         UINT flags2, ID3DBlob **code, ID3DBlob **errors);
D3DCOMPILERAPI HRESULT WINAPI D3DCompile2(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                          ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                          UINT flags2, UINT secondary_flags, LPCVOID secondary,
                                          SIZE_T secondary_size, ID3DBlob **code, ID3DBlob **errors);
D3DCOMPILERAPI HRESULT WINAPI D3DCompileFromFile(LPCWSTR file, const D3D_SHADER_MACRO *defines,
                                                 ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                                 UINT flags2, ID3DBlob **code, ID3DBlob **errors);
D3DCOMPILERAPI HRESULT WINAPI D3DPreprocess(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                            ID3DInclude *include, ID3DBlob **text, ID3DBlob **errors);
D3DCOMPILERAPI HRESULT WINAPI D3DAssemble(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                          ID3DInclude *include, UINT flags, ID3DBlob **code, ID3DBlob **errors);

D3DCOMPILERAPI HRESULT WINAPI D3DCreateLinker(ID3D11Linker **linker);
D3DCOMPILERAPI HRESULT WINAPI D3DLoadModule(LPCVOID data, SIZE_T size, ID3D11Module **module);
D3DCOMPILERAPI HRESULT WINAPI D3DCreateFunctionLinkingGraph(UINT flags, ID3D11FunctionLinkingGraph **graph);

#endif
