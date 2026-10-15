/*
 * direct3d 10: devices and the objects they create, state blocks and the
 * shader helpers of d3d10.dll. the interfaces and their method order are
 * those of d3d11.dll's vtables, which carry them.
 */
#ifndef MIME_WIN32_D3D10_H
#define MIME_WIN32_D3D10_H

#include <mime/win32/dxgi.h>
#include <mime/win32/d3dcommon.h>
#include <mime/win32/d3d10shader.h>

#define D3D10_SDK_VERSION 29
#define D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT 14
#define D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT 128
#define D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT 16
#define D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT 16
#define D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT 8
#define D3D10_SO_BUFFER_SLOT_COUNT 4
#define D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE 16
#define D3D10_APPEND_ALIGNED_ELEMENT 0xFFFFFFFF
#define D3D10_DEFAULT_SAMPLE_MASK 0xFFFFFFFF

#define D3D10_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS ((HRESULT)0x88790001)
#define D3D10_ERROR_FILE_NOT_FOUND ((HRESULT)0x88790002)

typedef enum D3D10_DRIVER_TYPE {
    D3D10_DRIVER_TYPE_HARDWARE = 0,
    D3D10_DRIVER_TYPE_REFERENCE = 1,
    D3D10_DRIVER_TYPE_NULL = 2,
    D3D10_DRIVER_TYPE_SOFTWARE = 3,
    D3D10_DRIVER_TYPE_WARP = 5
} D3D10_DRIVER_TYPE;

typedef enum D3D10_CREATE_DEVICE_FLAG {
    D3D10_CREATE_DEVICE_SINGLETHREADED = 0x1,
    D3D10_CREATE_DEVICE_DEBUG = 0x2,
    D3D10_CREATE_DEVICE_SWITCH_TO_REF = 0x4,
    D3D10_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS = 0x8,
    D3D10_CREATE_DEVICE_ALLOW_NULL_FROM_MAP = 0x10,
    D3D10_CREATE_DEVICE_BGRA_SUPPORT = 0x20,
    D3D10_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY = 0x80,
    D3D10_CREATE_DEVICE_STRICT_VALIDATION = 0x200,
    D3D10_CREATE_DEVICE_DEBUGGABLE = 0x400
} D3D10_CREATE_DEVICE_FLAG;

typedef enum D3D10_RESOURCE_DIMENSION {
    D3D10_RESOURCE_DIMENSION_UNKNOWN = 0,
    D3D10_RESOURCE_DIMENSION_BUFFER = 1,
    D3D10_RESOURCE_DIMENSION_TEXTURE1D = 2,
    D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3,
    D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4
} D3D10_RESOURCE_DIMENSION;

typedef enum D3D10_USAGE {
    D3D10_USAGE_DEFAULT = 0,
    D3D10_USAGE_IMMUTABLE = 1,
    D3D10_USAGE_DYNAMIC = 2,
    D3D10_USAGE_STAGING = 3
} D3D10_USAGE;

typedef enum D3D10_BIND_FLAG {
    D3D10_BIND_VERTEX_BUFFER = 0x1,
    D3D10_BIND_INDEX_BUFFER = 0x2,
    D3D10_BIND_CONSTANT_BUFFER = 0x4,
    D3D10_BIND_SHADER_RESOURCE = 0x8,
    D3D10_BIND_STREAM_OUTPUT = 0x10,
    D3D10_BIND_RENDER_TARGET = 0x20,
    D3D10_BIND_DEPTH_STENCIL = 0x40
} D3D10_BIND_FLAG;

typedef enum D3D10_CPU_ACCESS_FLAG {
    D3D10_CPU_ACCESS_WRITE = 0x10000,
    D3D10_CPU_ACCESS_READ = 0x20000
} D3D10_CPU_ACCESS_FLAG;

/* direct3d 11 moved the last two up a nibble. */
typedef enum D3D10_RESOURCE_MISC_FLAG {
    D3D10_RESOURCE_MISC_GENERATE_MIPS = 0x1,
    D3D10_RESOURCE_MISC_SHARED = 0x2,
    D3D10_RESOURCE_MISC_TEXTURECUBE = 0x4,
    D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX = 0x10,
    D3D10_RESOURCE_MISC_GDI_COMPATIBLE = 0x20
} D3D10_RESOURCE_MISC_FLAG;

typedef enum D3D10_MAP {
    D3D10_MAP_READ = 1,
    D3D10_MAP_WRITE = 2,
    D3D10_MAP_READ_WRITE = 3,
    D3D10_MAP_WRITE_DISCARD = 4,
    D3D10_MAP_WRITE_NO_OVERWRITE = 5
} D3D10_MAP;

#define D3D10_MAP_FLAG_DO_NOT_WAIT 0x100000
#define D3D10_ASYNC_GETDATA_DONOTFLUSH 0x1
#define D3D10_CLEAR_DEPTH 0x1
#define D3D10_CLEAR_STENCIL 0x2

typedef D3D_PRIMITIVE_TOPOLOGY D3D10_PRIMITIVE_TOPOLOGY;
#define D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED D3D_PRIMITIVE_TOPOLOGY_UNDEFINED
#define D3D10_PRIMITIVE_TOPOLOGY_POINTLIST D3D_PRIMITIVE_TOPOLOGY_POINTLIST
#define D3D10_PRIMITIVE_TOPOLOGY_LINELIST D3D_PRIMITIVE_TOPOLOGY_LINELIST
#define D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP D3D_PRIMITIVE_TOPOLOGY_LINESTRIP
#define D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
#define D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
#define D3D10_PRIMITIVE_TOPOLOGY_LINELIST_ADJ D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ
#define D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ
#define D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ
#define D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ
typedef D3D_SRV_DIMENSION D3D10_SRV_DIMENSION;
#define D3D10_SRV_DIMENSION_UNKNOWN D3D_SRV_DIMENSION_UNKNOWN
#define D3D10_SRV_DIMENSION_BUFFER D3D_SRV_DIMENSION_BUFFER
#define D3D10_SRV_DIMENSION_TEXTURE1D D3D_SRV_DIMENSION_TEXTURE1D
#define D3D10_SRV_DIMENSION_TEXTURE1DARRAY D3D_SRV_DIMENSION_TEXTURE1DARRAY
#define D3D10_SRV_DIMENSION_TEXTURE2D D3D_SRV_DIMENSION_TEXTURE2D
#define D3D10_SRV_DIMENSION_TEXTURE2DARRAY D3D_SRV_DIMENSION_TEXTURE2DARRAY
#define D3D10_SRV_DIMENSION_TEXTURE2DMS D3D_SRV_DIMENSION_TEXTURE2DMS
#define D3D10_SRV_DIMENSION_TEXTURE2DMSARRAY D3D_SRV_DIMENSION_TEXTURE2DMSARRAY
#define D3D10_SRV_DIMENSION_TEXTURE3D D3D_SRV_DIMENSION_TEXTURE3D
#define D3D10_SRV_DIMENSION_TEXTURECUBE D3D_SRV_DIMENSION_TEXTURECUBE
typedef RECT D3D10_RECT;

typedef struct D3D10_BUFFER_DESC {
    UINT ByteWidth;
    D3D10_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D10_BUFFER_DESC;

typedef struct D3D10_TEXTURE1D_DESC {
    UINT Width;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    D3D10_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D10_TEXTURE1D_DESC;

typedef struct D3D10_TEXTURE2D_DESC {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D10_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D10_TEXTURE2D_DESC;

typedef struct D3D10_TEXTURE3D_DESC {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    DXGI_FORMAT Format;
    D3D10_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D10_TEXTURE3D_DESC;

typedef struct D3D10_SUBRESOURCE_DATA {
    const void *pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
} D3D10_SUBRESOURCE_DATA;

typedef struct D3D10_MAPPED_TEXTURE2D {
    void *pData;
    UINT RowPitch;
} D3D10_MAPPED_TEXTURE2D;

typedef struct D3D10_MAPPED_TEXTURE3D {
    void *pData;
    UINT RowPitch;
    UINT DepthPitch;
} D3D10_MAPPED_TEXTURE3D;

typedef struct D3D10_BOX {
    UINT left;
    UINT top;
    UINT front;
    UINT right;
    UINT bottom;
    UINT back;
} D3D10_BOX;

/* the origin and size in whole pixels, unlike direct3d 11's. */
typedef struct D3D10_VIEWPORT {
    INT TopLeftX;
    INT TopLeftY;
    UINT Width;
    UINT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
} D3D10_VIEWPORT;

typedef struct D3D10_BUFFER_SRV {
    union {
        UINT FirstElement;
        UINT ElementOffset;
    };
    union {
        UINT NumElements;
        UINT ElementWidth;
    };
} D3D10_BUFFER_SRV;

typedef struct D3D10_TEX1D_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
} D3D10_TEX1D_SRV;

typedef struct D3D10_TEX1D_ARRAY_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX1D_ARRAY_SRV;

typedef struct D3D10_TEX2D_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
} D3D10_TEX2D_SRV;

typedef struct D3D10_TEX2D_ARRAY_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX2D_ARRAY_SRV;

typedef struct D3D10_TEX2DMS_SRV {
    UINT UnusedField_NothingToDefine;
} D3D10_TEX2DMS_SRV;

typedef struct D3D10_TEX2DMS_ARRAY_SRV {
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX2DMS_ARRAY_SRV;

typedef struct D3D10_TEX3D_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
} D3D10_TEX3D_SRV;

typedef struct D3D10_TEXCUBE_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
} D3D10_TEXCUBE_SRV;

typedef struct D3D10_SHADER_RESOURCE_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D10_SRV_DIMENSION ViewDimension;
    union {
        D3D10_BUFFER_SRV Buffer;
        D3D10_TEX1D_SRV Texture1D;
        D3D10_TEX1D_ARRAY_SRV Texture1DArray;
        D3D10_TEX2D_SRV Texture2D;
        D3D10_TEX2D_ARRAY_SRV Texture2DArray;
        D3D10_TEX2DMS_SRV Texture2DMS;
        D3D10_TEX2DMS_ARRAY_SRV Texture2DMSArray;
        D3D10_TEX3D_SRV Texture3D;
        D3D10_TEXCUBE_SRV TextureCube;
    };
} D3D10_SHADER_RESOURCE_VIEW_DESC;

typedef enum D3D10_RTV_DIMENSION {
    D3D10_RTV_DIMENSION_UNKNOWN = 0,
    D3D10_RTV_DIMENSION_BUFFER = 1,
    D3D10_RTV_DIMENSION_TEXTURE1D = 2,
    D3D10_RTV_DIMENSION_TEXTURE1DARRAY = 3,
    D3D10_RTV_DIMENSION_TEXTURE2D = 4,
    D3D10_RTV_DIMENSION_TEXTURE2DARRAY = 5,
    D3D10_RTV_DIMENSION_TEXTURE2DMS = 6,
    D3D10_RTV_DIMENSION_TEXTURE2DMSARRAY = 7,
    D3D10_RTV_DIMENSION_TEXTURE3D = 8
} D3D10_RTV_DIMENSION;

typedef struct D3D10_BUFFER_RTV {
    union {
        UINT FirstElement;
        UINT ElementOffset;
    };
    union {
        UINT NumElements;
        UINT ElementWidth;
    };
} D3D10_BUFFER_RTV;

typedef struct D3D10_TEX1D_RTV {
    UINT MipSlice;
} D3D10_TEX1D_RTV;

typedef struct D3D10_TEX1D_ARRAY_RTV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX1D_ARRAY_RTV;

typedef struct D3D10_TEX2D_RTV {
    UINT MipSlice;
} D3D10_TEX2D_RTV;

typedef struct D3D10_TEX2D_ARRAY_RTV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX2D_ARRAY_RTV;

typedef struct D3D10_TEX2DMS_RTV {
    UINT UnusedField_NothingToDefine;
} D3D10_TEX2DMS_RTV;

typedef struct D3D10_TEX2DMS_ARRAY_RTV {
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX2DMS_ARRAY_RTV;

typedef struct D3D10_TEX3D_RTV {
    UINT MipSlice;
    UINT FirstWSlice;
    UINT WSize;
} D3D10_TEX3D_RTV;

typedef struct D3D10_RENDER_TARGET_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D10_RTV_DIMENSION ViewDimension;
    union {
        D3D10_BUFFER_RTV Buffer;
        D3D10_TEX1D_RTV Texture1D;
        D3D10_TEX1D_ARRAY_RTV Texture1DArray;
        D3D10_TEX2D_RTV Texture2D;
        D3D10_TEX2D_ARRAY_RTV Texture2DArray;
        D3D10_TEX2DMS_RTV Texture2DMS;
        D3D10_TEX2DMS_ARRAY_RTV Texture2DMSArray;
        D3D10_TEX3D_RTV Texture3D;
    };
} D3D10_RENDER_TARGET_VIEW_DESC;

typedef enum D3D10_DSV_DIMENSION {
    D3D10_DSV_DIMENSION_UNKNOWN = 0,
    D3D10_DSV_DIMENSION_TEXTURE1D = 1,
    D3D10_DSV_DIMENSION_TEXTURE1DARRAY = 2,
    D3D10_DSV_DIMENSION_TEXTURE2D = 3,
    D3D10_DSV_DIMENSION_TEXTURE2DARRAY = 4,
    D3D10_DSV_DIMENSION_TEXTURE2DMS = 5,
    D3D10_DSV_DIMENSION_TEXTURE2DMSARRAY = 6
} D3D10_DSV_DIMENSION;

typedef struct D3D10_TEX1D_DSV {
    UINT MipSlice;
} D3D10_TEX1D_DSV;

typedef struct D3D10_TEX1D_ARRAY_DSV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX1D_ARRAY_DSV;

typedef struct D3D10_TEX2D_DSV {
    UINT MipSlice;
} D3D10_TEX2D_DSV;

typedef struct D3D10_TEX2D_ARRAY_DSV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX2D_ARRAY_DSV;

typedef struct D3D10_TEX2DMS_DSV {
    UINT UnusedField_NothingToDefine;
} D3D10_TEX2DMS_DSV;

typedef struct D3D10_TEX2DMS_ARRAY_DSV {
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D10_TEX2DMS_ARRAY_DSV;

/* no flags: read only depth views came with direct3d 11. */
typedef struct D3D10_DEPTH_STENCIL_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D10_DSV_DIMENSION ViewDimension;
    union {
        D3D10_TEX1D_DSV Texture1D;
        D3D10_TEX1D_ARRAY_DSV Texture1DArray;
        D3D10_TEX2D_DSV Texture2D;
        D3D10_TEX2D_ARRAY_DSV Texture2DArray;
        D3D10_TEX2DMS_DSV Texture2DMS;
        D3D10_TEX2DMS_ARRAY_DSV Texture2DMSArray;
    };
} D3D10_DEPTH_STENCIL_VIEW_DESC;

typedef enum D3D10_INPUT_CLASSIFICATION {
    D3D10_INPUT_PER_VERTEX_DATA = 0,
    D3D10_INPUT_PER_INSTANCE_DATA = 1
} D3D10_INPUT_CLASSIFICATION;

typedef struct D3D10_INPUT_ELEMENT_DESC {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    D3D10_INPUT_CLASSIFICATION InputSlotClass;
    UINT InstanceDataStepRate;
} D3D10_INPUT_ELEMENT_DESC;

typedef struct D3D10_SO_DECLARATION_ENTRY {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    BYTE StartComponent;
    BYTE ComponentCount;
    BYTE OutputSlot;
} D3D10_SO_DECLARATION_ENTRY;

typedef enum D3D10_BLEND {
    D3D10_BLEND_ZERO = 1,
    D3D10_BLEND_ONE = 2,
    D3D10_BLEND_SRC_COLOR = 3,
    D3D10_BLEND_INV_SRC_COLOR = 4,
    D3D10_BLEND_SRC_ALPHA = 5,
    D3D10_BLEND_INV_SRC_ALPHA = 6,
    D3D10_BLEND_DEST_ALPHA = 7,
    D3D10_BLEND_INV_DEST_ALPHA = 8,
    D3D10_BLEND_DEST_COLOR = 9,
    D3D10_BLEND_INV_DEST_COLOR = 10,
    D3D10_BLEND_SRC_ALPHA_SAT = 11,
    D3D10_BLEND_BLEND_FACTOR = 14,
    D3D10_BLEND_INV_BLEND_FACTOR = 15,
    D3D10_BLEND_SRC1_COLOR = 16,
    D3D10_BLEND_INV_SRC1_COLOR = 17,
    D3D10_BLEND_SRC1_ALPHA = 18,
    D3D10_BLEND_INV_SRC1_ALPHA = 19
} D3D10_BLEND;

typedef enum D3D10_BLEND_OP {
    D3D10_BLEND_OP_ADD = 1,
    D3D10_BLEND_OP_SUBTRACT = 2,
    D3D10_BLEND_OP_REV_SUBTRACT = 3,
    D3D10_BLEND_OP_MIN = 4,
    D3D10_BLEND_OP_MAX = 5
} D3D10_BLEND_OP;

#define D3D10_COLOR_WRITE_ENABLE_ALL 15

/* one blend for every target, each target enabling it and masking its channels. */
typedef struct D3D10_BLEND_DESC {
    BOOL AlphaToCoverageEnable;
    BOOL BlendEnable[8];
    D3D10_BLEND SrcBlend;
    D3D10_BLEND DestBlend;
    D3D10_BLEND_OP BlendOp;
    D3D10_BLEND SrcBlendAlpha;
    D3D10_BLEND DestBlendAlpha;
    D3D10_BLEND_OP BlendOpAlpha;
    UINT8 RenderTargetWriteMask[8];
} D3D10_BLEND_DESC;

typedef enum D3D10_COMPARISON_FUNC {
    D3D10_COMPARISON_NEVER = 1,
    D3D10_COMPARISON_LESS = 2,
    D3D10_COMPARISON_EQUAL = 3,
    D3D10_COMPARISON_LESS_EQUAL = 4,
    D3D10_COMPARISON_GREATER = 5,
    D3D10_COMPARISON_NOT_EQUAL = 6,
    D3D10_COMPARISON_GREATER_EQUAL = 7,
    D3D10_COMPARISON_ALWAYS = 8
} D3D10_COMPARISON_FUNC;

typedef enum D3D10_DEPTH_WRITE_MASK {
    D3D10_DEPTH_WRITE_MASK_ZERO = 0,
    D3D10_DEPTH_WRITE_MASK_ALL = 1
} D3D10_DEPTH_WRITE_MASK;

typedef enum D3D10_STENCIL_OP {
    D3D10_STENCIL_OP_KEEP = 1,
    D3D10_STENCIL_OP_ZERO = 2,
    D3D10_STENCIL_OP_REPLACE = 3,
    D3D10_STENCIL_OP_INCR_SAT = 4,
    D3D10_STENCIL_OP_DECR_SAT = 5,
    D3D10_STENCIL_OP_INVERT = 6,
    D3D10_STENCIL_OP_INCR = 7,
    D3D10_STENCIL_OP_DECR = 8
} D3D10_STENCIL_OP;

typedef struct D3D10_DEPTH_STENCILOP_DESC {
    D3D10_STENCIL_OP StencilFailOp;
    D3D10_STENCIL_OP StencilDepthFailOp;
    D3D10_STENCIL_OP StencilPassOp;
    D3D10_COMPARISON_FUNC StencilFunc;
} D3D10_DEPTH_STENCILOP_DESC;

typedef struct D3D10_DEPTH_STENCIL_DESC {
    BOOL DepthEnable;
    D3D10_DEPTH_WRITE_MASK DepthWriteMask;
    D3D10_COMPARISON_FUNC DepthFunc;
    BOOL StencilEnable;
    UINT8 StencilReadMask;
    UINT8 StencilWriteMask;
    D3D10_DEPTH_STENCILOP_DESC FrontFace;
    D3D10_DEPTH_STENCILOP_DESC BackFace;
} D3D10_DEPTH_STENCIL_DESC;

typedef enum D3D10_FILL_MODE {
    D3D10_FILL_WIREFRAME = 2,
    D3D10_FILL_SOLID = 3
} D3D10_FILL_MODE;

typedef enum D3D10_CULL_MODE {
    D3D10_CULL_NONE = 1,
    D3D10_CULL_FRONT = 2,
    D3D10_CULL_BACK = 3
} D3D10_CULL_MODE;

typedef struct D3D10_RASTERIZER_DESC {
    D3D10_FILL_MODE FillMode;
    D3D10_CULL_MODE CullMode;
    BOOL FrontCounterClockwise;
    INT DepthBias;
    FLOAT DepthBiasClamp;
    FLOAT SlopeScaledDepthBias;
    BOOL DepthClipEnable;
    BOOL ScissorEnable;
    BOOL MultisampleEnable;
    BOOL AntialiasedLineEnable;
} D3D10_RASTERIZER_DESC;

/* the same encoding as direct3d 11's filters. */
typedef enum D3D10_FILTER {
    D3D10_FILTER_MIN_MAG_MIP_POINT = 0x00,
    D3D10_FILTER_MIN_MAG_MIP_LINEAR = 0x15,
    D3D10_FILTER_ANISOTROPIC = 0x55,
    D3D10_FILTER_COMPARISON_MIN_MAG_MIP_POINT = 0x80,
    D3D10_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR = 0x95
} D3D10_FILTER;

typedef enum D3D10_TEXTURE_ADDRESS_MODE {
    D3D10_TEXTURE_ADDRESS_WRAP = 1,
    D3D10_TEXTURE_ADDRESS_MIRROR = 2,
    D3D10_TEXTURE_ADDRESS_CLAMP = 3,
    D3D10_TEXTURE_ADDRESS_BORDER = 4,
    D3D10_TEXTURE_ADDRESS_MIRROR_ONCE = 5
} D3D10_TEXTURE_ADDRESS_MODE;

typedef struct D3D10_SAMPLER_DESC {
    D3D10_FILTER Filter;
    D3D10_TEXTURE_ADDRESS_MODE AddressU;
    D3D10_TEXTURE_ADDRESS_MODE AddressV;
    D3D10_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    D3D10_COMPARISON_FUNC ComparisonFunc;
    FLOAT BorderColor[4];
    FLOAT MinLOD;
    FLOAT MaxLOD;
} D3D10_SAMPLER_DESC;

typedef enum D3D10_QUERY {
    D3D10_QUERY_EVENT = 0,
    D3D10_QUERY_OCCLUSION = 1,
    D3D10_QUERY_TIMESTAMP = 2,
    D3D10_QUERY_TIMESTAMP_DISJOINT = 3,
    D3D10_QUERY_PIPELINE_STATISTICS = 4,
    D3D10_QUERY_OCCLUSION_PREDICATE = 5,
    D3D10_QUERY_SO_STATISTICS = 6,
    D3D10_QUERY_SO_OVERFLOW_PREDICATE = 7
} D3D10_QUERY;

#define D3D10_QUERY_MISC_PREDICATEHINT 0x1

typedef struct D3D10_QUERY_DESC {
    D3D10_QUERY Query;
    UINT MiscFlags;
} D3D10_QUERY_DESC;

typedef struct D3D10_QUERY_DATA_TIMESTAMP_DISJOINT {
    UINT64 Frequency;
    BOOL Disjoint;
} D3D10_QUERY_DATA_TIMESTAMP_DISJOINT;

/* direct3d 11's without the hull, domain and compute shader counts. */
typedef struct D3D10_QUERY_DATA_PIPELINE_STATISTICS {
    UINT64 IAVertices;
    UINT64 IAPrimitives;
    UINT64 VSInvocations;
    UINT64 GSInvocations;
    UINT64 GSPrimitives;
    UINT64 CInvocations;
    UINT64 CPrimitives;
    UINT64 PSInvocations;
} D3D10_QUERY_DATA_PIPELINE_STATISTICS;

typedef struct D3D10_QUERY_DATA_SO_STATISTICS {
    UINT64 NumPrimitivesWritten;
    UINT64 PrimitivesStorageNeeded;
} D3D10_QUERY_DATA_SO_STATISTICS;

typedef enum D3D10_COUNTER {
    D3D10_COUNTER_GPU_IDLE = 0,
    D3D10_COUNTER_DEVICE_DEPENDENT_0 = 0x40000000
} D3D10_COUNTER;

typedef enum D3D10_COUNTER_TYPE {
    D3D10_COUNTER_TYPE_FLOAT32 = 0,
    D3D10_COUNTER_TYPE_UINT16 = 1,
    D3D10_COUNTER_TYPE_UINT32 = 2,
    D3D10_COUNTER_TYPE_UINT64 = 3
} D3D10_COUNTER_TYPE;

typedef struct D3D10_COUNTER_DESC {
    D3D10_COUNTER Counter;
    UINT MiscFlags;
} D3D10_COUNTER_DESC;

typedef struct D3D10_COUNTER_INFO {
    D3D10_COUNTER LastDeviceDependentCounter;
    UINT NumSimultaneousCounters;
    UINT8 NumDetectableParallelUnits;
} D3D10_COUNTER_INFO;

/* what a state block captures, a bit per state and slot, 76 bytes in all. */
typedef struct D3D10_STATE_BLOCK_MASK {
    BYTE VS;
    BYTE VSSamplers[2];
    BYTE VSShaderResources[16];
    BYTE VSConstantBuffers[2];
    BYTE GS;
    BYTE GSSamplers[2];
    BYTE GSShaderResources[16];
    BYTE GSConstantBuffers[2];
    BYTE PS;
    BYTE PSSamplers[2];
    BYTE PSShaderResources[16];
    BYTE PSConstantBuffers[2];
    BYTE IAVertexBuffers[2];
    BYTE IAIndexBuffer;
    BYTE IAInputLayout;
    BYTE IAPrimitiveTopology;
    BYTE OMRenderTargets;
    BYTE OMDepthStencilState;
    BYTE OMBlendState;
    BYTE RSViewports;
    BYTE RSScissorRects;
    BYTE RSRasterizerState;
    BYTE SOBuffers;
    BYTE Predication;
} D3D10_STATE_BLOCK_MASK;

typedef enum D3D10_DEVICE_STATE_TYPES {
    D3D10_DST_SO_BUFFERS = 1,
    D3D10_DST_OM_RENDER_TARGETS,
    D3D10_DST_OM_DEPTH_STENCIL_STATE,
    D3D10_DST_OM_BLEND_STATE,
    D3D10_DST_VS,
    D3D10_DST_VS_SAMPLERS,
    D3D10_DST_VS_SHADER_RESOURCES,
    D3D10_DST_VS_CONSTANT_BUFFERS,
    D3D10_DST_GS,
    D3D10_DST_GS_SAMPLERS,
    D3D10_DST_GS_SHADER_RESOURCES,
    D3D10_DST_GS_CONSTANT_BUFFERS,
    D3D10_DST_PS,
    D3D10_DST_PS_SAMPLERS,
    D3D10_DST_PS_SHADER_RESOURCES,
    D3D10_DST_PS_CONSTANT_BUFFERS,
    D3D10_DST_IA_VERTEX_BUFFERS,
    D3D10_DST_IA_INDEX_BUFFER,
    D3D10_DST_IA_INPUT_LAYOUT,
    D3D10_DST_IA_PRIMITIVE_TOPOLOGY,
    D3D10_DST_RS_VIEWPORTS,
    D3D10_DST_RS_SCISSOR_RECTS,
    D3D10_DST_RS_RASTERIZER_STATE,
    D3D10_DST_PREDICATION
} D3D10_DEVICE_STATE_TYPES;

typedef D3D_SHADER_MACRO D3D10_SHADER_MACRO;
typedef ID3D10Blob *LPD3D10BLOB;

typedef struct ID3D10DeviceChild ID3D10DeviceChild;
typedef struct ID3D10Resource ID3D10Resource;
typedef struct ID3D10Buffer ID3D10Buffer;
typedef struct ID3D10Texture1D ID3D10Texture1D;
typedef struct ID3D10Texture2D ID3D10Texture2D;
typedef struct ID3D10Texture3D ID3D10Texture3D;
typedef struct ID3D10View ID3D10View;
typedef struct ID3D10ShaderResourceView ID3D10ShaderResourceView;
typedef struct ID3D10RenderTargetView ID3D10RenderTargetView;
typedef struct ID3D10DepthStencilView ID3D10DepthStencilView;
typedef struct ID3D10VertexShader ID3D10VertexShader;
typedef struct ID3D10GeometryShader ID3D10GeometryShader;
typedef struct ID3D10PixelShader ID3D10PixelShader;
typedef struct ID3D10InputLayout ID3D10InputLayout;
typedef struct ID3D10SamplerState ID3D10SamplerState;
typedef struct ID3D10BlendState ID3D10BlendState;
typedef struct ID3D10DepthStencilState ID3D10DepthStencilState;
typedef struct ID3D10RasterizerState ID3D10RasterizerState;
typedef struct ID3D10Asynchronous ID3D10Asynchronous;
typedef struct ID3D10Query ID3D10Query;
typedef struct ID3D10Predicate ID3D10Predicate;
typedef struct ID3D10Counter ID3D10Counter;
typedef struct ID3D10Device ID3D10Device;
typedef struct ID3D10Multithread ID3D10Multithread;
typedef struct ID3D10StateBlock ID3D10StateBlock;
typedef struct ID3D10Effect ID3D10Effect;
typedef struct ID3D10EffectPool ID3D10EffectPool;
typedef struct ID3DInclude ID3D10Include;

#define ID3D10DeviceChild_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetDevice)(T *This, ID3D10Device **device);                                                \
    HRESULT(STDMETHODCALLTYPE *GetPrivateData)(T *This, REFGUID guid, UINT *size, void *data);                         \
    HRESULT(STDMETHODCALLTYPE *SetPrivateData)(T *This, REFGUID guid, UINT size, const void *data);                    \
    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)(T *This, REFGUID guid, const IUnknown *data);

#define ID3D10Resource_METHODS(T)                                                                                      \
    ID3D10DeviceChild_METHODS(T)                                                                                       \
    void(STDMETHODCALLTYPE *GetType)(T *This, D3D10_RESOURCE_DIMENSION *dimension);                                    \
    void(STDMETHODCALLTYPE *SetEvictionPriority)(T *This, UINT priority);                                              \
    UINT(STDMETHODCALLTYPE *GetEvictionPriority)(T *This);

#define ID3D10Buffer_METHODS(T)                                                                                        \
    ID3D10Resource_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *Map)(T *This, D3D10_MAP type, UINT flags, void **data);                                 \
    void(STDMETHODCALLTYPE *Unmap)(T *This);                                                                           \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_BUFFER_DESC *desc);

#define ID3D10Texture1D_METHODS(T)                                                                                     \
    ID3D10Resource_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *Map)(T *This, UINT subresource, D3D10_MAP type, UINT flags, void **data);               \
    void(STDMETHODCALLTYPE *Unmap)(T *This, UINT subresource);                                                         \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_TEXTURE1D_DESC *desc);

#define ID3D10Texture2D_METHODS(T)                                                                                     \
    ID3D10Resource_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *Map)(T *This, UINT subresource, D3D10_MAP type, UINT flags,                             \
                                    D3D10_MAPPED_TEXTURE2D *mapped);                                                   \
    void(STDMETHODCALLTYPE *Unmap)(T *This, UINT subresource);                                                         \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_TEXTURE2D_DESC *desc);

#define ID3D10Texture3D_METHODS(T)                                                                                     \
    ID3D10Resource_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *Map)(T *This, UINT subresource, D3D10_MAP type, UINT flags,                             \
                                    D3D10_MAPPED_TEXTURE3D *mapped);                                                   \
    void(STDMETHODCALLTYPE *Unmap)(T *This, UINT subresource);                                                         \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_TEXTURE3D_DESC *desc);

#define ID3D10View_METHODS(T)                                                                                          \
    ID3D10DeviceChild_METHODS(T)                                                                                       \
    void(STDMETHODCALLTYPE *GetResource)(T *This, ID3D10Resource **resource);

#define ID3D10ShaderResourceView_METHODS(T)                                                                            \
    ID3D10View_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_SHADER_RESOURCE_VIEW_DESC *desc);

#define ID3D10RenderTargetView_METHODS(T)                                                                              \
    ID3D10View_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_RENDER_TARGET_VIEW_DESC *desc);

#define ID3D10DepthStencilView_METHODS(T)                                                                              \
    ID3D10View_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_DEPTH_STENCIL_VIEW_DESC *desc);

#define ID3D10VertexShader_METHODS(T) ID3D10DeviceChild_METHODS(T)
#define ID3D10GeometryShader_METHODS(T) ID3D10DeviceChild_METHODS(T)
#define ID3D10PixelShader_METHODS(T) ID3D10DeviceChild_METHODS(T)
#define ID3D10InputLayout_METHODS(T) ID3D10DeviceChild_METHODS(T)

#define ID3D10SamplerState_METHODS(T)                                                                                  \
    ID3D10DeviceChild_METHODS(T)                                                                                       \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_SAMPLER_DESC *desc);

#define ID3D10BlendState_METHODS(T)                                                                                    \
    ID3D10DeviceChild_METHODS(T)                                                                                       \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_BLEND_DESC *desc);

#define ID3D10DepthStencilState_METHODS(T)                                                                             \
    ID3D10DeviceChild_METHODS(T)                                                                                       \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_DEPTH_STENCIL_DESC *desc);

#define ID3D10RasterizerState_METHODS(T)                                                                               \
    ID3D10DeviceChild_METHODS(T)                                                                                       \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_RASTERIZER_DESC *desc);

#define ID3D10Asynchronous_METHODS(T)                                                                                  \
    ID3D10DeviceChild_METHODS(T)                                                                                       \
    void(STDMETHODCALLTYPE *Begin)(T *This);                                                                           \
    void(STDMETHODCALLTYPE *End)(T *This);                                                                             \
    HRESULT(STDMETHODCALLTYPE *GetData)(T *This, void *data, UINT size, UINT flags);                                   \
    UINT(STDMETHODCALLTYPE *GetDataSize)(T *This);

#define ID3D10Query_METHODS(T)                                                                                         \
    ID3D10Asynchronous_METHODS(T)                                                                                      \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_QUERY_DESC *desc);

#define ID3D10Predicate_METHODS(T) ID3D10Query_METHODS(T)

#define ID3D10Counter_METHODS(T)                                                                                       \
    ID3D10Asynchronous_METHODS(T)                                                                                      \
    void(STDMETHODCALLTYPE *GetDesc)(T *This, D3D10_COUNTER_DESC *desc);

#define ID3D10Device_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *VSSetConstantBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer *const *buffers);       \
    void(STDMETHODCALLTYPE *PSSetShaderResources)(T *This, UINT slot, UINT count,                                      \
                                                  ID3D10ShaderResourceView *const *views);                             \
    void(STDMETHODCALLTYPE *PSSetShader)(T *This, ID3D10PixelShader *shader);                                          \
    void(STDMETHODCALLTYPE *PSSetSamplers)(T *This, UINT slot, UINT count, ID3D10SamplerState *const *samplers);       \
    void(STDMETHODCALLTYPE *VSSetShader)(T *This, ID3D10VertexShader *shader);                                         \
    void(STDMETHODCALLTYPE *DrawIndexed)(T *This, UINT count, UINT start, INT base);                                   \
    void(STDMETHODCALLTYPE *Draw)(T *This, UINT count, UINT start);                                                    \
    void(STDMETHODCALLTYPE *PSSetConstantBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer *const *buffers);       \
    void(STDMETHODCALLTYPE *IASetInputLayout)(T *This, ID3D10InputLayout *layout);                                     \
    void(STDMETHODCALLTYPE *IASetVertexBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer *const *buffers,          \
                                                const UINT *strides, const UINT *offsets);                             \
    void(STDMETHODCALLTYPE *IASetIndexBuffer)(T *This, ID3D10Buffer *buffer, DXGI_FORMAT format, UINT offset);         \
    void(STDMETHODCALLTYPE *DrawIndexedInstanced)(T *This, UINT count, UINT instances, UINT start, INT base,           \
                                                  UINT start_instance);                                                \
    void(STDMETHODCALLTYPE *DrawInstanced)(T *This, UINT count, UINT instances, UINT start, UINT start_instance);      \
    void(STDMETHODCALLTYPE *GSSetConstantBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer *const *buffers);       \
    void(STDMETHODCALLTYPE *GSSetShader)(T *This, ID3D10GeometryShader *shader);                                       \
    void(STDMETHODCALLTYPE *IASetPrimitiveTopology)(T *This, D3D10_PRIMITIVE_TOPOLOGY topology);                       \
    void(STDMETHODCALLTYPE *VSSetShaderResources)(T *This, UINT slot, UINT count,                                      \
                                                  ID3D10ShaderResourceView *const *views);                             \
    void(STDMETHODCALLTYPE *VSSetSamplers)(T *This, UINT slot, UINT count, ID3D10SamplerState *const *samplers);       \
    void(STDMETHODCALLTYPE *SetPredication)(T *This, ID3D10Predicate *predicate, BOOL value);                          \
    void(STDMETHODCALLTYPE *GSSetShaderResources)(T *This, UINT slot, UINT count,                                      \
                                                  ID3D10ShaderResourceView *const *views);                             \
    void(STDMETHODCALLTYPE *GSSetSamplers)(T *This, UINT slot, UINT count, ID3D10SamplerState *const *samplers);       \
    void(STDMETHODCALLTYPE *OMSetRenderTargets)(T *This, UINT count, ID3D10RenderTargetView *const *views,             \
                                                ID3D10DepthStencilView *depth);                                        \
    void(STDMETHODCALLTYPE *OMSetBlendState)(T *This, ID3D10BlendState *state, const FLOAT factor[4], UINT mask);      \
    void(STDMETHODCALLTYPE *OMSetDepthStencilState)(T *This, ID3D10DepthStencilState *state, UINT ref);                \
    void(STDMETHODCALLTYPE *SOSetTargets)(T *This, UINT count, ID3D10Buffer *const *buffers, const UINT *offsets);     \
    void(STDMETHODCALLTYPE *DrawAuto)(T *This);                                                                        \
    void(STDMETHODCALLTYPE *RSSetState)(T *This, ID3D10RasterizerState *state);                                        \
    void(STDMETHODCALLTYPE *RSSetViewports)(T *This, UINT count, const D3D10_VIEWPORT *viewports);                     \
    void(STDMETHODCALLTYPE *RSSetScissorRects)(T *This, UINT count, const D3D10_RECT *rects);                          \
    void(STDMETHODCALLTYPE *CopySubresourceRegion)(T *This, ID3D10Resource *dst, UINT dst_sub, UINT x, UINT y,         \
                                                   UINT z, ID3D10Resource *src, UINT src_sub, const D3D10_BOX *box);   \
    void(STDMETHODCALLTYPE *CopyResource)(T *This, ID3D10Resource *dst, ID3D10Resource *src);                          \
    void(STDMETHODCALLTYPE *UpdateSubresource)(T *This, ID3D10Resource *dst, UINT dst_sub, const D3D10_BOX *box,       \
                                               const void *data, UINT row_pitch, UINT depth_pitch);                    \
    void(STDMETHODCALLTYPE *ClearRenderTargetView)(T *This, ID3D10RenderTargetView *view, const FLOAT color[4]);       \
    void(STDMETHODCALLTYPE *ClearDepthStencilView)(T *This, ID3D10DepthStencilView *view, UINT flags, FLOAT depth,     \
                                                   UINT8 stencil);                                                     \
    void(STDMETHODCALLTYPE *GenerateMips)(T *This, ID3D10ShaderResourceView *view);                                    \
    void(STDMETHODCALLTYPE *ResolveSubresource)(T *This, ID3D10Resource *dst, UINT dst_sub, ID3D10Resource *src,       \
                                                UINT src_sub, DXGI_FORMAT format);                                     \
    void(STDMETHODCALLTYPE *VSGetConstantBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer **buffers);             \
    void(STDMETHODCALLTYPE *PSGetShaderResources)(T *This, UINT slot, UINT count, ID3D10ShaderResourceView **views);   \
    void(STDMETHODCALLTYPE *PSGetShader)(T *This, ID3D10PixelShader **shader);                                         \
    void(STDMETHODCALLTYPE *PSGetSamplers)(T *This, UINT slot, UINT count, ID3D10SamplerState **samplers);             \
    void(STDMETHODCALLTYPE *VSGetShader)(T *This, ID3D10VertexShader **shader);                                        \
    void(STDMETHODCALLTYPE *PSGetConstantBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer **buffers);             \
    void(STDMETHODCALLTYPE *IAGetInputLayout)(T *This, ID3D10InputLayout **layout);                                    \
    void(STDMETHODCALLTYPE *IAGetVertexBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer **buffers,                \
                                                UINT *strides, UINT *offsets);                                         \
    void(STDMETHODCALLTYPE *IAGetIndexBuffer)(T *This, ID3D10Buffer **buffer, DXGI_FORMAT *format, UINT *offset);      \
    void(STDMETHODCALLTYPE *GSGetConstantBuffers)(T *This, UINT slot, UINT count, ID3D10Buffer **buffers);             \
    void(STDMETHODCALLTYPE *GSGetShader)(T *This, ID3D10GeometryShader **shader);                                      \
    void(STDMETHODCALLTYPE *IAGetPrimitiveTopology)(T *This, D3D10_PRIMITIVE_TOPOLOGY *topology);                      \
    void(STDMETHODCALLTYPE *VSGetShaderResources)(T *This, UINT slot, UINT count, ID3D10ShaderResourceView **views);   \
    void(STDMETHODCALLTYPE *VSGetSamplers)(T *This, UINT slot, UINT count, ID3D10SamplerState **samplers);             \
    void(STDMETHODCALLTYPE *GetPredication)(T *This, ID3D10Predicate **predicate, BOOL *value);                        \
    void(STDMETHODCALLTYPE *GSGetShaderResources)(T *This, UINT slot, UINT count, ID3D10ShaderResourceView **views);   \
    void(STDMETHODCALLTYPE *GSGetSamplers)(T *This, UINT slot, UINT count, ID3D10SamplerState **samplers);             \
    void(STDMETHODCALLTYPE *OMGetRenderTargets)(T *This, UINT count, ID3D10RenderTargetView **views,                   \
                                                ID3D10DepthStencilView **depth);                                       \
    void(STDMETHODCALLTYPE *OMGetBlendState)(T *This, ID3D10BlendState **state, FLOAT factor[4], UINT *mask);          \
    void(STDMETHODCALLTYPE *OMGetDepthStencilState)(T *This, ID3D10DepthStencilState **state, UINT *ref);              \
    void(STDMETHODCALLTYPE *SOGetTargets)(T *This, UINT count, ID3D10Buffer **buffers, UINT *offsets);                 \
    void(STDMETHODCALLTYPE *RSGetState)(T *This, ID3D10RasterizerState **state);                                       \
    void(STDMETHODCALLTYPE *RSGetViewports)(T *This, UINT *count, D3D10_VIEWPORT *viewports);                          \
    void(STDMETHODCALLTYPE *RSGetScissorRects)(T *This, UINT *count, D3D10_RECT *rects);                               \
    HRESULT(STDMETHODCALLTYPE *GetDeviceRemovedReason)(T *This);                                                       \
    HRESULT(STDMETHODCALLTYPE *SetExceptionMode)(T *This, UINT flags);                                                 \
    UINT(STDMETHODCALLTYPE *GetExceptionMode)(T *This);                                                                \
    HRESULT(STDMETHODCALLTYPE *GetPrivateData)(T *This, REFGUID guid, UINT *size, void *data);                         \
    HRESULT(STDMETHODCALLTYPE *SetPrivateData)(T *This, REFGUID guid, UINT size, const void *data);                    \
    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)(T *This, REFGUID guid, const IUnknown *data);                  \
    void(STDMETHODCALLTYPE *ClearState)(T *This);                                                                      \
    void(STDMETHODCALLTYPE *Flush)(T *This);                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateBuffer)(T *This, const D3D10_BUFFER_DESC *desc,                                   \
                                             const D3D10_SUBRESOURCE_DATA *data, ID3D10Buffer **buffer);               \
    HRESULT(STDMETHODCALLTYPE *CreateTexture1D)(T *This, const D3D10_TEXTURE1D_DESC *desc,                             \
                                                const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture1D **texture);        \
    HRESULT(STDMETHODCALLTYPE *CreateTexture2D)(T *This, const D3D10_TEXTURE2D_DESC *desc,                             \
                                                const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture2D **texture);        \
    HRESULT(STDMETHODCALLTYPE *CreateTexture3D)(T *This, const D3D10_TEXTURE3D_DESC *desc,                             \
                                                const D3D10_SUBRESOURCE_DATA *data, ID3D10Texture3D **texture);        \
    HRESULT(STDMETHODCALLTYPE *CreateShaderResourceView)(T *This, ID3D10Resource *resource,                            \
                                                         const D3D10_SHADER_RESOURCE_VIEW_DESC *desc,                  \
                                                         ID3D10ShaderResourceView **view);                             \
    HRESULT(STDMETHODCALLTYPE *CreateRenderTargetView)(T *This, ID3D10Resource *resource,                              \
                                                       const D3D10_RENDER_TARGET_VIEW_DESC *desc,                      \
                                                       ID3D10RenderTargetView **view);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateDepthStencilView)(T *This, ID3D10Resource *resource,                              \
                                                       const D3D10_DEPTH_STENCIL_VIEW_DESC *desc,                      \
                                                       ID3D10DepthStencilView **view);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateInputLayout)(T *This, const D3D10_INPUT_ELEMENT_DESC *elements, UINT count,       \
                                                  const void *bytecode, SIZE_T length, ID3D10InputLayout **layout);    \
    HRESULT(STDMETHODCALLTYPE *CreateVertexShader)(T *This, const void *bytecode, SIZE_T length,                       \
                                                   ID3D10VertexShader **shader);                                       \
    HRESULT(STDMETHODCALLTYPE *CreateGeometryShader)(T *This, const void *bytecode, SIZE_T length,                     \
                                                     ID3D10GeometryShader **shader);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateGeometryShaderWithStreamOutput)(                                                  \
        T * This, const void *bytecode, SIZE_T length, const D3D10_SO_DECLARATION_ENTRY *entries, UINT count,          \
        UINT stride, ID3D10GeometryShader **shader);                                                                   \
    HRESULT(STDMETHODCALLTYPE *CreatePixelShader)(T *This, const void *bytecode, SIZE_T length,                        \
                                                  ID3D10PixelShader **shader);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateBlendState)(T *This, const D3D10_BLEND_DESC *desc, ID3D10BlendState **state);     \
    HRESULT(STDMETHODCALLTYPE *CreateDepthStencilState)(T *This, const D3D10_DEPTH_STENCIL_DESC *desc,                 \
                                                        ID3D10DepthStencilState **state);                              \
    HRESULT(STDMETHODCALLTYPE *CreateRasterizerState)(T *This, const D3D10_RASTERIZER_DESC *desc,                      \
                                                      ID3D10RasterizerState **state);                                  \
    HRESULT(STDMETHODCALLTYPE *CreateSamplerState)(T *This, const D3D10_SAMPLER_DESC *desc,                            \
                                                   ID3D10SamplerState **state);                                        \
    HRESULT(STDMETHODCALLTYPE *CreateQuery)(T *This, const D3D10_QUERY_DESC *desc, ID3D10Query **query);               \
    HRESULT(STDMETHODCALLTYPE *CreatePredicate)(T *This, const D3D10_QUERY_DESC *desc, ID3D10Predicate **predicate);   \
    HRESULT(STDMETHODCALLTYPE *CreateCounter)(T *This, const D3D10_COUNTER_DESC *desc, ID3D10Counter **counter);       \
    HRESULT(STDMETHODCALLTYPE *CheckFormatSupport)(T *This, DXGI_FORMAT format, UINT *support);                        \
    HRESULT(STDMETHODCALLTYPE *CheckMultisampleQualityLevels)(T *This, DXGI_FORMAT format, UINT samples,               \
                                                              UINT *levels);                                           \
    void(STDMETHODCALLTYPE *CheckCounterInfo)(T *This, D3D10_COUNTER_INFO *info);                                      \
    HRESULT(STDMETHODCALLTYPE *CheckCounter)(T *This, const D3D10_COUNTER_DESC *desc, D3D10_COUNTER_TYPE *type,        \
                                             UINT *active, LPSTR name, UINT *name_length, LPSTR units,                 \
                                             UINT *units_length, LPSTR description, UINT *description_length);         \
    UINT(STDMETHODCALLTYPE *GetCreationFlags)(T *This);                                                                \
    HRESULT(STDMETHODCALLTYPE *OpenSharedResource)(T *This, HANDLE handle, REFIID riid, void **resource);              \
    void(STDMETHODCALLTYPE *SetTextFilterSize)(T *This, UINT width, UINT height);                                      \
    void(STDMETHODCALLTYPE *GetTextFilterSize)(T *This, UINT *width, UINT *height);

#define ID3D10Multithread_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *Enter)(T *This);                                                                           \
    void(STDMETHODCALLTYPE *Leave)(T *This);                                                                           \
    BOOL(STDMETHODCALLTYPE *SetMultithreadProtected)(T *This, BOOL protect);                                           \
    BOOL(STDMETHODCALLTYPE *GetMultithreadProtected)(T *This);

#define ID3D10StateBlock_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Capture)(T *This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *Apply)(T *This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *ReleaseAllDeviceObjects)(T *This);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetDevice)(T *This, ID3D10Device **device);

MIME_COM_INTERFACE(ID3D10DeviceChild)
MIME_COM_INTERFACE(ID3D10Resource)
MIME_COM_INTERFACE(ID3D10Buffer)
MIME_COM_INTERFACE(ID3D10Texture1D)
MIME_COM_INTERFACE(ID3D10Texture2D)
MIME_COM_INTERFACE(ID3D10Texture3D)
MIME_COM_INTERFACE(ID3D10View)
MIME_COM_INTERFACE(ID3D10ShaderResourceView)
MIME_COM_INTERFACE(ID3D10RenderTargetView)
MIME_COM_INTERFACE(ID3D10DepthStencilView)
MIME_COM_INTERFACE(ID3D10VertexShader)
MIME_COM_INTERFACE(ID3D10GeometryShader)
MIME_COM_INTERFACE(ID3D10PixelShader)
MIME_COM_INTERFACE(ID3D10InputLayout)
MIME_COM_INTERFACE(ID3D10SamplerState)
MIME_COM_INTERFACE(ID3D10BlendState)
MIME_COM_INTERFACE(ID3D10DepthStencilState)
MIME_COM_INTERFACE(ID3D10RasterizerState)
MIME_COM_INTERFACE(ID3D10Asynchronous)
MIME_COM_INTERFACE(ID3D10Query)
MIME_COM_INTERFACE(ID3D10Predicate)
MIME_COM_INTERFACE(ID3D10Counter)
MIME_COM_INTERFACE(ID3D10Device)
MIME_COM_INTERFACE(ID3D10Multithread)
MIME_COM_INTERFACE(ID3D10StateBlock)

MIME_DEFINE_GUID(IID_ID3D10DeviceChild, 0x9B7E4C00, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Resource, 0x9B7E4C01, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Buffer, 0x9B7E4C02, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Texture1D, 0x9B7E4C03, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Texture2D, 0x9B7E4C04, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Texture3D, 0x9B7E4C05, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10View, 0xC902B03F, 0x60A7, 0x49BA, 0x99, 0x36, 0x2A, 0x3A, 0xB3, 0x7A, 0x7E, 0x33);
MIME_DEFINE_GUID(IID_ID3D10ShaderResourceView, 0x9B7E4C07, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89,
                 0xF0);
MIME_DEFINE_GUID(IID_ID3D10RenderTargetView, 0x9B7E4C08, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89,
                 0xF0);
MIME_DEFINE_GUID(IID_ID3D10DepthStencilView, 0x9B7E4C09, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89,
                 0xF0);
MIME_DEFINE_GUID(IID_ID3D10VertexShader, 0x9B7E4C0A, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10GeometryShader, 0x6316BE88, 0x54CD, 0x4040, 0xAB, 0x44, 0x20, 0x46, 0x1B, 0xC8, 0x1F, 0x68);
MIME_DEFINE_GUID(IID_ID3D10PixelShader, 0x4968B601, 0x9D00, 0x4CDE, 0x83, 0x46, 0x8E, 0x7F, 0x67, 0x58, 0x19, 0xB6);
MIME_DEFINE_GUID(IID_ID3D10InputLayout, 0x9B7E4C0B, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10SamplerState, 0x9B7E4C0C, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10BlendState, 0xEDAD8D19, 0x8A35, 0x4D6D, 0x85, 0x66, 0x2E, 0xA2, 0x76, 0xCD, 0xE1, 0x61);
MIME_DEFINE_GUID(IID_ID3D10DepthStencilState, 0x2B4B1CC8, 0xA4AD, 0x41F8, 0x83, 0x22, 0xCA, 0x86, 0xFC, 0x3E, 0xC6,
                 0x75);
MIME_DEFINE_GUID(IID_ID3D10RasterizerState, 0xA2A07292, 0x89AF, 0x4345, 0xBE, 0x2E, 0xC5, 0x3D, 0x9F, 0xBB, 0x6E,
                 0x9F);
MIME_DEFINE_GUID(IID_ID3D10Asynchronous, 0x9B7E4C0D, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Query, 0x9B7E4C0E, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Predicate, 0x9B7E4C10, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Counter, 0x9B7E4C11, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Device, 0x9B7E4C0F, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10Multithread, 0x9B7E4E00, 0x342C, 0x4106, 0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0);
MIME_DEFINE_GUID(IID_ID3D10StateBlock, 0x0803425A, 0x57F5, 0x4DD6, 0x94, 0x65, 0xA8, 0x75, 0x70, 0x83, 0x4A, 0x08);
MIME_DEFINE_GUID(IID_ID3D10Effect, 0x51B0CA8B, 0xEC0B, 0x4519, 0x87, 0x0D, 0x8E, 0xE1, 0xCB, 0x50, 0x17, 0xC7);
MIME_DEFINE_GUID(IID_ID3D10EffectPool, 0x9537AB04, 0x3250, 0x412E, 0x82, 0x13, 0xFC, 0xD2, 0xF8, 0x67, 0x79, 0x33);

#include <mime/win32/d3d10effect.h>

#ifndef MIME_BUILD_D3D10
#define D3D10API DECLSPEC_IMPORT
#else
#define D3D10API DECLSPEC_EXPORT
#endif

D3D10API HRESULT WINAPI D3D10CreateDevice(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type, HMODULE software, UINT flags,
                                          UINT sdk, ID3D10Device **device);
D3D10API HRESULT WINAPI D3D10CreateDeviceAndSwapChain(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type, HMODULE software,
                                                      UINT flags, UINT sdk, DXGI_SWAP_CHAIN_DESC *desc,
                                                      IDXGISwapChain **swapchain, ID3D10Device **device);
D3D10API HRESULT WINAPI D3D10CreateBlob(SIZE_T size, ID3D10Blob **blob);
D3D10API UINT64 WINAPI D3D10GetVersion(void);
D3D10API HRESULT WINAPI D3D10RegisterLayers(void);

D3D10API HRESULT WINAPI D3D10CompileShader(LPCSTR source, SIZE_T length, LPCSTR file, const D3D10_SHADER_MACRO *defines,
                                           ID3D10Include *include, LPCSTR entry, LPCSTR profile, UINT flags,
                                           ID3D10Blob **shader, ID3D10Blob **errors);
D3D10API HRESULT WINAPI D3D10DisassembleShader(const void *bytecode, SIZE_T length, BOOL color, LPCSTR comments,
                                               ID3D10Blob **disassembly);
D3D10API HRESULT WINAPI D3D10PreprocessShader(LPCSTR source, SIZE_T length, LPCSTR file,
                                              const D3D10_SHADER_MACRO *defines, ID3D10Include *include,
                                              ID3D10Blob **text, ID3D10Blob **errors);
D3D10API HRESULT WINAPI D3D10ReflectShader(const void *bytecode, SIZE_T length, ID3D10ShaderReflection **reflector);
D3D10API LPCSTR WINAPI D3D10GetVertexShaderProfile(ID3D10Device *device);
D3D10API LPCSTR WINAPI D3D10GetGeometryShaderProfile(ID3D10Device *device);
D3D10API LPCSTR WINAPI D3D10GetPixelShaderProfile(ID3D10Device *device);
D3D10API HRESULT WINAPI D3D10GetInputSignatureBlob(const void *bytecode, SIZE_T length, ID3D10Blob **blob);
D3D10API HRESULT WINAPI D3D10GetOutputSignatureBlob(const void *bytecode, SIZE_T length, ID3D10Blob **blob);
D3D10API HRESULT WINAPI D3D10GetInputAndOutputSignatureBlob(const void *bytecode, SIZE_T length, ID3D10Blob **blob);
D3D10API HRESULT WINAPI D3D10GetShaderDebugInfo(const void *bytecode, SIZE_T length, ID3D10Blob **blob);

D3D10API HRESULT WINAPI D3D10CreateEffectFromMemory(void *data, SIZE_T length, UINT flags, ID3D10Device *device,
                                                    ID3D10EffectPool *pool, ID3D10Effect **effect);
D3D10API HRESULT WINAPI D3D10CreateEffectPoolFromMemory(void *data, SIZE_T length, UINT flags, ID3D10Device *device,
                                                        ID3D10EffectPool **pool);
D3D10API HRESULT WINAPI D3D10CompileEffectFromMemory(void *data, SIZE_T length, LPCSTR file,
                                                     const D3D10_SHADER_MACRO *defines, ID3D10Include *include,
                                                     UINT hlsl_flags, UINT fx_flags, ID3D10Blob **effect,
                                                     ID3D10Blob **errors);
D3D10API HRESULT WINAPI D3D10DisassembleEffect(ID3D10Effect *effect, BOOL color, ID3D10Blob **disassembly);

D3D10API HRESULT WINAPI D3D10CreateStateBlock(ID3D10Device *device, D3D10_STATE_BLOCK_MASK *mask,
                                              ID3D10StateBlock **block);
D3D10API HRESULT WINAPI D3D10StateBlockMaskUnion(D3D10_STATE_BLOCK_MASK *a, D3D10_STATE_BLOCK_MASK *b,
                                                 D3D10_STATE_BLOCK_MASK *result);
D3D10API HRESULT WINAPI D3D10StateBlockMaskIntersect(D3D10_STATE_BLOCK_MASK *a, D3D10_STATE_BLOCK_MASK *b,
                                                     D3D10_STATE_BLOCK_MASK *result);
D3D10API HRESULT WINAPI D3D10StateBlockMaskDifference(D3D10_STATE_BLOCK_MASK *a, D3D10_STATE_BLOCK_MASK *b,
                                                      D3D10_STATE_BLOCK_MASK *result);
D3D10API HRESULT WINAPI D3D10StateBlockMaskEnableCapture(D3D10_STATE_BLOCK_MASK *mask, D3D10_DEVICE_STATE_TYPES type,
                                                         UINT start, UINT count);
D3D10API HRESULT WINAPI D3D10StateBlockMaskDisableCapture(D3D10_STATE_BLOCK_MASK *mask,
                                                          D3D10_DEVICE_STATE_TYPES type, UINT start, UINT count);
D3D10API HRESULT WINAPI D3D10StateBlockMaskEnableAll(D3D10_STATE_BLOCK_MASK *mask);
D3D10API HRESULT WINAPI D3D10StateBlockMaskDisableAll(D3D10_STATE_BLOCK_MASK *mask);
D3D10API BOOL WINAPI D3D10StateBlockMaskGetSetting(D3D10_STATE_BLOCK_MASK *mask, D3D10_DEVICE_STATE_TYPES type,
                                                   UINT entry);

#endif
