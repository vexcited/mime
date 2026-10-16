/*
 * D3DDisassemble: shader model 4 and 5 bytecode as fxc prints it. the
 * header comments come from the reflection, as the real disassembler
 * builds them: the creator, required features, buffer definitions,
 * resource bindings, signatures and tessellation, then the instructions.
 */
#include "d3dcompiler_private.h"
#include "../lib/dxbc/internal.h"

static UINT read32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }

typedef struct Disasm {
    DcText text;
    UINT flags;
    /* shader model 5.1 and later number registers with ranges and spaces. */
    BOOL sm51;
    BOOL hex;
    UINT indent;
} Disasm;

static void put(Disasm *d, const char *s) { DcTextPuts(&d->text, s); }

static BOOL nul_within(const BYTE *p, UINT size)
{
    for (UINT i = 0; i < size; i++)
        if (!p[i])
            return TRUE;
    return FALSE;
}

static void out(Disasm *d, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    DcTextVprintf(&d->text, format, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ the header */

static const char *const REQUIRES[] = {
    "Double-precision floating point",
    "Early depth-stencil",
    "UAVs at every shader stage",
    "64 UAV slots",
    "Minimum-precision data types",
    "Double-precision extensions for 11.1",
    "Shader extensions for 11.1",
    "Comparison filtering for feature level 9",
    "Tiled resources",
    "PS Output Stencil Ref",
    "PS Inner Coverage",
    "Typed UAV Load Additional Formats",
    "Raster Ordered UAVs",
    "SV_RenderTargetArrayIndex or SV_ViewportArrayIndex from any shader feeding rasterizer",
};

static const char *type_string(UINT type)
{
    static const char *const NAMES[] = {
        "void",          "bool",           "int",            "float",          "string",
        "Texture",       "Texture1D",      "Texture2D",      "Texture3D",      "TextureCube",
        "SamplerState",  "sampler1D",      "sampler2D",      "sampler3D",      "samplerCUBE",
        "PixelShader",   "VertexShader",   "pixelfragment",  "vertexfragment", "uint",
        "byte",          "GeometryShader", "RasterizerState", "DepthStencilState", "BlendState",
        "Buffer",        "UNKNOWN",        "UNKNOWN",        "Texture1DArray", "Texture2DArray",
        "RenderTargetView", "DepthStencilView", "Texture2DMS", "Texture2DMSArray", "TextureCubeArray",
        "HullShader",    "DomainShader",   "interface",      "ComputeShader",  "double",
    };
    if (type < sizeof(NAMES) / sizeof(NAMES[0]))
        return NAMES[type];
    switch (type) {
    case 52:
        return "min8float";
    case 53:
        return "min10float";
    case 54:
        return "min16float";
    case 55:
        return "min12int";
    case 56:
        return "min16int";
    case 57:
        return "min16uint";
    }
    return "UNKNOWN";
}

static void indent(Disasm *d, UINT depth)
{
    put(d, "//   ");
    for (UINT i = 0; i < depth; i++)
        put(d, "    ");
}

/* a variable or member as hlsl declares it, members of structures with their offsets. */
static HRESULT print_type(Disasm *d, const char *name, ID3D11ShaderReflectionType *type, UINT depth, UINT index,
                          BOOL *after_struct, UINT offset)
{
    indent(d, depth);
    D3D11_SHADER_TYPE_DESC desc;
    memset(&desc, 0, sizeof(desc));
    HRESULT hr = type->lpVtbl->GetDesc(type, &desc);
    if (FAILED(hr))
        return hr;
    if (!name)
        return E_FAIL;
    const char *ts = type_string(desc.Type);
    /* a structure after the first variable and the first member of any structure start on a new line. */
    if (*after_struct || (index && desc.Class == D3D_SVC_STRUCT)) {
        put(d, "\n");
        indent(d, depth);
    }
    *after_struct = desc.Class == D3D_SVC_STRUCT;
    switch (desc.Class) {
    case D3D_SVC_SCALAR:
    case D3D_SVC_OBJECT:
        out(d, "%s", ts);
        break;
    case D3D_SVC_VECTOR:
        out(d, "%s%u", ts, desc.Columns);
        break;
    case D3D_SVC_MATRIX_ROWS:
        out(d, "row_major %s%ux%u", ts, desc.Rows, desc.Columns);
        break;
    case D3D_SVC_MATRIX_COLUMNS:
        out(d, "%s%ux%u", ts, desc.Rows, desc.Columns);
        break;
    case D3D_SVC_STRUCT:
        if (desc.Name)
            out(d, "struct %s\n", desc.Name);
        else
            put(d, "struct\n");
        indent(d, depth);
        put(d, "{\n");
        for (UINT i = 0; i < desc.Members; i++) {
            ID3D11ShaderReflectionType *member = type->lpVtbl->GetMemberTypeByIndex(type, i);
            D3D11_SHADER_TYPE_DESC member_desc;
            memset(&member_desc, 0, sizeof(member_desc));
            if (FAILED(hr = member->lpVtbl->GetDesc(member, &member_desc)))
                return hr;
            if (FAILED(hr = print_type(d, type->lpVtbl->GetMemberTypeName(type, i), member, depth + 1, i,
                                       after_struct, offset + member_desc.Offset)))
                return hr;
            DcTextAlign(&d->text, 40);
            out(d, "// Offset: %4u\n", offset + member_desc.Offset);
        }
        put(d, "//\n");
        indent(d, depth);
        put(d, "}");
        break;
    case D3D_SVC_INTERFACE_POINTER:
        if (desc.Name)
            out(d, "%s %s", ts, desc.Name);
        else
            out(d, "%s", ts);
        break;
    default:
        break;
    }
    out(d, " %s", name);
    if (desc.Elements > 1)
        out(d, "[%u]", desc.Elements);
    put(d, ";");
    return S_OK;
}

static HRESULT print_buffers(Disasm *d, ID3D11ShaderReflection *r, UINT count)
{
    put(d, "//\n// Buffer Definitions: \n//\n");
    for (UINT i = 0; i < count; i++) {
        ID3D11ShaderReflectionConstantBuffer *b = r->lpVtbl->GetConstantBufferByIndex(r, i);
        D3D11_SHADER_BUFFER_DESC desc;
        HRESULT hr = b->lpVtbl->GetDesc(b, &desc);
        if (FAILED(hr))
            return hr;
        if (!desc.Variables)
            continue;
        static const char *const KINDS[] = {"cbuffer", "tbuffer", "interfaces", "Resource bind info for"};
        out(d, "// %s %s\n// {\n//\n", desc.Type < 4 ? KINDS[desc.Type] : "<unknown buffer type>", desc.Name);
        BOOL after_struct = FALSE;
        for (UINT j = 0; j < desc.Variables; j++) {
            ID3D11ShaderReflectionVariable *v = b->lpVtbl->GetVariableByIndex(b, j);
            D3D11_SHADER_VARIABLE_DESC vd;
            if (FAILED(hr = v->lpVtbl->GetDesc(v, &vd)))
                return hr;
            if (FAILED(hr = print_type(d, vd.Name, v->lpVtbl->GetType(v), 0, j, &after_struct, vd.StartOffset)))
                return hr;
            DcTextAlign(&d->text, 40);
            const char *unused = vd.uFlags & D3D_SVF_USED ? "" : " [unused]";
            if (desc.Type == D3D_CT_INTERFACE_POINTERS && !(vd.uFlags & D3D_SVF_USED)) {
                put(d, "// Offset:  N/A Size:   N/A [unused]\n");
            } else {
                out(d, "// Offset: %4u Size: %5u%s\n", vd.StartOffset, vd.Size, unused);
                if (vd.DefaultValue) {
                    for (UINT k = 0; 4 * k < vd.Size; k += 4) {
                        put(d, "// ");
                        if (k) {
                            DcTextAlign(&d->text, 10);
                        } else {
                            DcTextAlign(&d->text, 8);
                            put(d, "= ");
                        }
                        for (UINT w = 0; w < 4 && 4 * (k + w) < vd.Size; w++)
                            out(d, "0x%08x ", read32((const BYTE *)vd.DefaultValue + 4 * (k + w)));
                        put(d, "\n");
                    }
                }
            }
            if (vd.TextureSize || vd.SamplerSize) {
                DcTextAlign(&d->text, 40);
                put(d, "// ");
                if (vd.TextureSize == 1)
                    out(d, "Texture:   t%u", vd.StartTexture);
                else if (vd.TextureSize > 1)
                    out(d, "Textures:  t%u-t%u", vd.StartTexture, vd.StartTexture + vd.TextureSize - 1);
                if (vd.TextureSize && vd.SamplerSize)
                    put(d, ", ");
                if (vd.SamplerSize == 1)
                    out(d, "Sampler: s%u", vd.StartSampler);
                else if (vd.SamplerSize > 1)
                    out(d, "Samplers: s%u-s%u", vd.StartSampler, vd.StartSampler + vd.SamplerSize - 1);
                put(d, "\n");
            }
        }
        put(d, "//\n// }\n//\n");
    }
    return S_OK;
}

static void print_bindings(Disasm *d, ID3D11ShaderReflection *r, UINT count)
{
    put(d, "//\n// Resource Bindings:\n//\n");
    if (d->sm51) {
        put(d, "// Name                                 Type  Format         Dim      ID      HLSL Bind  Count\n");
        put(d, "// ------------------------------ ---------- ------- ----------- ------- -------------- ------\n");
    } else {
        put(d, "// Name                                 Type  Format         Dim      HLSL Bind  Count\n");
        put(d, "// ------------------------------ ---------- ------- ----------- -------------- ------\n");
    }
    for (UINT i = 0; i < count; i++) {
        DcBindDesc b;
        memset(&b, 0, sizeof(b));
        if (FAILED(r->lpVtbl->GetResourceBindingDesc(r, i, (D3D11_SHADER_INPUT_BIND_DESC *)&b)))
            return;
        const char *type = "void", *prefix = "bug", *upper = "BUG";
        UINT components = 0;
        switch (b.Type) {
        case D3D_SIT_CBUFFER:
            type = "cbuffer", prefix = "cb", upper = "CB";
            break;
        case D3D_SIT_TBUFFER:
            type = "tbuffer", prefix = "t", upper = "T";
            break;
        case D3D_SIT_TEXTURE:
        case D3D_SIT_UAV_RWTYPED:
            if (b.Type == D3D_SIT_TEXTURE)
                type = "texture", prefix = "t", upper = "T";
            else
                type = "UAV", prefix = "u", upper = "U";
            components = ((b.uFlags >> 2) & 3) + 1;
            break;
        case D3D_SIT_SAMPLER:
            type = b.uFlags & D3D_SIF_COMPARISON_SAMPLER ? "sampler_c" : "sampler", prefix = "s", upper = "S";
            break;
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS:
            type = "texture", prefix = "t", upper = "T";
            break;
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            type = "UAV", prefix = "u", upper = "U";
            break;
        }
        const char *format = "NA", *dim = "NA";
        char ms[32];
        switch (b.Type) {
        case D3D_SIT_TEXTURE:
        case D3D_SIT_UAV_RWTYPED: {
            static const char *const DIMS[] = {"NA", "buf", "1d", "1darray", "2d", "2darray", "2d", "2darray",
                                               "3d", "cube", "cubearray"};
            static const char *const FORMATS[] = {"<unknown resource return type>", "unorm", "snorm", "sint",
                                                  "uint", "float", "mixed", "double", "<continued>"};
            dim = b.Dimension < 11 ? DIMS[b.Dimension] : "NA";
            format = b.ReturnType < 9 ? FORMATS[b.ReturnType] : FORMATS[0];
            if (b.Dimension == D3D_SRV_DIMENSION_TEXTURE2DMS || b.Dimension == D3D_SRV_DIMENSION_TEXTURE2DMSARRAY) {
                DcText t = {};
                if (b.NumSamples)
                    DcTextPrintf(&t, "%sMS%d", dim, b.NumSamples);
                else
                    DcTextPrintf(&t, "%sMS", dim);
                if (t.buf && t.length < sizeof(ms)) {
                    memcpy(ms, t.buf, t.length + 1);
                    dim = ms;
                }
                DcTextFree(&t);
            }
            break;
        }
        case D3D_SIT_STRUCTURED:
            dim = "r/o", format = "struct";
            break;
        case D3D_SIT_UAV_RWSTRUCTURED:
            dim = "r/w", format = "struct";
            break;
        case D3D_SIT_BYTEADDRESS:
            dim = "r/o", format = "byte";
            break;
        case D3D_SIT_UAV_RWBYTEADDRESS:
            dim = "r/w", format = "byte";
            break;
        case D3D_SIT_UAV_APPEND_STRUCTURED:
            dim = "append", format = "struct";
            break;
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
            dim = "consume", format = "struct";
            break;
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            dim = "r/w+cnt", format = "struct";
            break;
        }
        DcText bind = {}, id = {}, count_text = {};
        if (b.Space)
            DcTextPrintf(&bind, "%s%u,space%u", prefix, b.BindPoint, b.Space);
        else
            DcTextPrintf(&bind, "%s%u", prefix, b.BindPoint);
        DcTextPrintf(&id, "%s%u", upper, b.uID);
        if (b.BindCount)
            DcTextPrintf(&count_text, "%u", b.BindCount);
        else
            DcTextPuts(&count_text, "unbounded");
        const char *unused = b.uFlags & D3D_SIF_UNUSED ? " [unused]" : "";
        const char *bs = bind.buf ? bind.buf : "", *is = id.buf ? id.buf : "", *cs = count_text.buf ? count_text.buf : "";
        if (components > 1) {
            if (d->sm51)
                out(d, "// %-30s %10s %6s%d %11s %7s %14s %6s %s\n", b.Name, type, format, components, dim, is, bs, cs,
                    unused);
            else
                out(d, "// %-30s %10s %6s%d %11s %14s %6s %s\n", b.Name, type, format, components, dim, bs, cs, unused);
        } else {
            if (d->sm51)
                out(d, "// %-30s %10s %7s %11s %7s %14s %6s %s\n", b.Name, type, format, dim, is, bs, cs, unused);
            else
                out(d, "// %-30s %10s %7s %11s %14s %6s %s\n", b.Name, type, format, dim, bs, cs, unused);
        }
        DcTextFree(&bind);
        DcTextFree(&id);
        DcTextFree(&count_text);
    }
    put(d, "//\n");
}

static const char *system_value_name(UINT value)
{
    static const char *const LOW[] = {"NONE",   "POS",    "CLIPDST",  "CULLDST", "RTINDEX", "VPINDEX",
                                      "VERTID", "PRIMID", "INSTID",   "FFACE",   "SAMPLE",  "QUADEDGE",
                                      "QUADINT", "TRIEDGE", "TRIINT", "LINEDET", "LINEDEN"};
    static const char *const HIGH[] = {"TARGET", "DEPTH", "COVERAGE", "DEPTHGE", "DEPTHLE", "STENCILREF", "INNERCOV"};
    if (value < 17)
        return LOW[value];
    if (value >= 64 && value < 71)
        return HIGH[value - 64];
    return "NONE";
}

static void mask_chars(Disasm *d, UINT mask)
{
    out(d, "   %c%c%c%c", mask & 1 ? 'x' : ' ', mask & 2 ? 'y' : ' ', mask & 4 ? 'z' : ' ', mask & 8 ? 'w' : ' ');
}

/* inputs show the components read, outputs those always written, which the chunk stores inverted. */
static void print_signature(Disasm *d, ID3D11ShaderReflection *r, const char *title, UINT which, UINT count,
                            BOOL input)
{
    out(d, "//\n// %s signature:\n//\n", title);
    put(d, "// Name                 Index   Mask Register SysValue  Format   Used\n");
    put(d, "// -------------------- ----- ------ -------- -------- ------- ------\n");
    if (!count) {
        out(d, "// no %s\n", title);
        return;
    }
    HRESULT(STDMETHODCALLTYPE * get)(ID3D11ShaderReflection *, UINT, D3D11_SIGNATURE_PARAMETER_DESC *) =
        which == 0 ? r->lpVtbl->GetInputParameterDesc
                   : which == 1 ? r->lpVtbl->GetOutputParameterDesc : r->lpVtbl->GetPatchConstantParameterDesc;
    BOOL streams = FALSE;
    for (UINT i = 0; i < count; i++) {
        D3D11_SIGNATURE_PARAMETER_DESC p;
        memset(&p, 0, sizeof(p));
        get(r, i, &p);
        if (p.Stream)
            streams = TRUE;
    }
    for (UINT i = 0; i < count; i++) {
        D3D11_SIGNATURE_PARAMETER_DESC p;
        memset(&p, 0, sizeof(p));
        get(r, i, &p);
        put(d, "// ");
        if (streams) {
            out(d, "m%u:", p.Stream);
            out(d, "%-17s", p.SemanticName);
        } else {
            out(d, "%-20s", p.SemanticName);
        }
        out(d, " %5u", p.SemanticIndex);
        if (p.Register != 0xFFFFFFFF) {
            mask_chars(d, p.Mask);
            out(d, " %8u", p.Register);
        } else if (!_stricmp(p.SemanticName, "SV_Depth")) {
            put(d, "    N/A   oDepth");
        } else if (!_stricmp(p.SemanticName, "SV_DepthGreaterEqual")) {
            put(d, "    N/A oDepthGE");
        } else if (!_stricmp(p.SemanticName, "SV_DepthLessEqual")) {
            put(d, "    N/A oDepthLE");
        } else if (!_stricmp(p.SemanticName, "SV_Coverage")) {
            put(d, "    N/A    oMask");
        } else if (!_stricmp(p.SemanticName, "SV_StencilRef")) {
            put(d, "    N/A    oStencilRef");
        } else if (p.SystemValueType == D3D_NAME_PRIMITIVE_ID) {
            put(d, "    N/A   primID");
        } else {
            put(d, "    N/A  special");
        }
        out(d, " %8s", system_value_name(p.SystemValueType));
        const char *format = "unknown";
        switch (p.ComponentType) {
        case D3D_REGISTER_COMPONENT_UINT32:
            format = p.MinPrecision == D3D_MIN_PRECISION_UINT_16 ? "min16u" : "uint";
            break;
        case D3D_REGISTER_COMPONENT_SINT32:
            format = p.MinPrecision == D3D_MIN_PRECISION_SINT_16 ? "min16i" : "int";
            break;
        case D3D_REGISTER_COMPONENT_FLOAT32:
            format = p.MinPrecision == D3D_MIN_PRECISION_FLOAT_16
                         ? "min16f"
                         : p.MinPrecision == D3D_MIN_PRECISION_FLOAT_2_8 ? "min2_8f" : "float";
            break;
        default:
            break;
        }
        out(d, " %7s", format);
        BYTE used = input ? p.ReadWriteMask : (BYTE)~p.ReadWriteMask;
        if (p.Register == 0xFFFFFFFF)
            put(d, used ? "    YES" : "     NO");
        else
            mask_chars(d, used);
        put(d, "\n");
    }
    put(d, "//\n");
}

static void print_tessellation(Disasm *d, const D3D11_SHADER_DESC *desc)
{
    if (desc->cControlPoints || desc->TessellatorDomain) {
        static const char *const DOMAINS[] = {"Undefined", "Isoline", "Triangle", "Quadrilateral"};
        put(d, "// Tessellation Domain   # of control points\n");
        put(d, "// -------------------- --------------------\n");
        out(d, "// %-20s %20u\n", desc->TessellatorDomain < 4 ? DOMAINS[desc->TessellatorDomain] : "<internal error>",
            desc->cControlPoints);
        put(d, "//\n");
    }
    if (!desc->HSOutputPrimitive && !desc->HSPartitioning)
        return;
    static const char *const PRIMITIVES[] = {"Undefined", "Point", "Line", "Clockwise Triangles",
                                             "Counter-Clockwise Triangles"};
    static const char *const PARTITIONINGS[] = {"Undefined", "Integer", "Integer Power of 2", "Odd Fractional",
                                                "Even Fractional"};
    put(d, "// Tessellation Output Primitive  Partitioning Type \n");
    put(d, "// ------------------------------ ------------------\n");
    out(d, "// %-30s %-18s\n", desc->HSOutputPrimitive <= 4 ? PRIMITIVES[desc->HSOutputPrimitive] : "<internal error>",
        desc->HSPartitioning <= 4 ? PARTITIONINGS[desc->HSPartitioning] : "<internal error>");
    put(d, "//\n");
}

static void print_header(Disasm *d, const DcContainer *c, ID3D11ShaderReflection *r, const char *comments)
{
    D3D11_SHADER_DESC desc;
    r->lpVtbl->GetDesc(r, &desc);
    out(d, "//\n// Generated by %s\n//\n", desc.Creator ? desc.Creator : "Microsoft (R) D3D Shader Disassembler");
    if (comments)
        put(d, comments);
    UINT64 requires = r->lpVtbl->GetRequiresFlags(r);
    if (requires) {
        put(d, "//\n// Note: shader requires additional functionality:\n");
        for (UINT i = 0; i < 14; i++)
            if (requires & (1ull << i))
                out(d, "//       %s\n", REQUIRES[i]);
        put(d, "//\n");
    }
    if (DcFindChunk(c, DC_TAG('R', 'D', 'E', 'F')) >= 0) {
        if (desc.ConstantBuffers)
            print_buffers(d, r, desc.ConstantBuffers);
        if (desc.BoundResources)
            print_bindings(d, r, desc.BoundResources);
        put(d, "//\n");
    }
    if (DcFindChunk(c, DC_TAG('P', 'C', 'S', 'G')) >= 0 || DcFindChunk(c, DC_TAG('P', 'S', 'G', '1')) >= 0)
        print_signature(d, r, "Patch Constant", 2, desc.PatchConstantParameters,
                        D3D11_SHVER_GET_TYPE(desc.Version) == D3D11_SHVER_DOMAIN_SHADER);
    if (DcFindChunk(c, DC_TAG('I', 'S', 'G', 'N')) >= 0 || DcFindChunk(c, DC_TAG('I', 'S', 'G', '1')) >= 0)
        print_signature(d, r, "Input", 0, desc.InputParameters, TRUE);
    if (DcFindChunk(c, DC_TAG('O', 'S', 'G', 'N')) >= 0 || DcFindChunk(c, DC_TAG('O', 'S', 'G', '5')) >= 0 ||
        DcFindChunk(c, DC_TAG('O', 'S', 'G', '1')) >= 0)
        print_signature(d, r, "Output", 1, desc.OutputParameters, FALSE);
    int name = DcFindChunk(c, DC_TAG('I', 'L', 'D', 'N'));
    if (name >= 0) {
        DcChunk chunk = DcGetChunk(c, name);
        if (chunk.size > 4 && chunk.data[4] && nul_within(chunk.data + 4, chunk.size - 4))
            out(d, "// Debug name: %s\n", (const char *)chunk.data + 4);
    }
    if (r->lpVtbl->IsSampleFrequencyShader(r))
        put(d, "// Pixel Shader runs at sample frequency\n//\n");
    print_tessellation(d, &desc);
}

/* ------------------------------------------------------------------ instructions */

static const char *const OPCODE_NAMES[235] = {
    "add", "and", "break", "breakc", "call", "callc", "case", "continue", "continuec", "cut", "default",
    "deriv_rtx", "deriv_rty", "discard", "div", "dp2", "dp3", "dp4", "else", "emit", "emit_then_cut", "endif",
    "endloop", "endswitch", "eq", "exp", "frc", "ftoi", "ftou", "ge", "iadd", "if", "ieq", "ige", "ilt", "imad",
    "imax", "imin", "imul", "ine", "ineg", "ishl", "ishr", "itof", "label", "ld", "ldms", "log", "loop", "lt", "mad",
    "min", "max", "customdata", "mov", "movc", "mul", "ne", "nop", "not", "or", "resinfo", "ret", "retc", "round_ne",
    "round_ni", "round_pi", "round_z", "rsq", "sample", "sample_c", "sample_c_lz", "sample_l", "sample_d", "sample_b",
    "sqrt", "switch", "sincos", "udiv", "ult", "uge", "umul", "umad", "umax", "umin", "ushr", "utof", "xor",
    "dcl_resource", "dcl_constantbuffer", "dcl_sampler", "dcl_indexrange", "dcl_outputtopology",
    "dcl_inputprimitive", "dcl_maxout", "dcl_input", "dcl_input_sgv", "dcl_input_siv", "dcl_input_ps",
    "dcl_input_ps_sgv", "dcl_input_ps_siv", "dcl_output", "dcl_output_sgv", "dcl_output_siv", "dcl_temps",
    "dcl_indexableTemp", "dcl_globalFlags", "jmp", "lod", "gather4", "samplepos", "sampleinfo", nullptr, "hs_decls",
    "hs_control_point_phase", "hs_fork_phase", "hs_join_phase", "emit_stream", "cut_stream",
    "emit_then_cut_stream", "fcall", "bufinfo", "deriv_rtx_coarse", "deriv_rtx_fine", "deriv_rty_coarse",
    "deriv_rty_fine", "gather4_c", "gather4_po", "gather4_po_c", "rcp", "f32tof16", "f16tof32", "uaddc", "usubb",
    "countbits", "firstbit_hi", "firstbit_lo", "firstbit_shi", "ubfe", "ibfe", "bfi", "bfrev", "swapc", "dcl_stream",
    "dcl_function_body", "dcl_function_table", "dcl_interface", "dcl_input_control_point_count",
    "dcl_output_control_point_count", "dcl_tessellator_domain", "dcl_tessellator_partitioning",
    "dcl_tessellator_output_primitive", "dcl_hs_max_tessfactor", "dcl_hs_fork_phase_instance_count",
    "dcl_hs_join_phase_instance_count", "dcl_thread_group", "dcl_uav_typed", "dcl_uav_raw", "dcl_uav_structured",
    "dcl_tgsm_raw", "dcl_tgsm_structured", "dcl_resource_raw", "dcl_resource_structured", "ld_uav_typed",
    "store_uav_typed", "ld_raw", "store_raw", "ld_structured", "store_structured", "atomic_and", "atomic_or",
    "atomic_xor", "atomic_cmp_store", "atomic_iadd", "atomic_imax", "atomic_imin", "atomic_umax", "atomic_umin",
    "imm_atomic_alloc", "imm_atomic_consume", "imm_atomic_iadd", "imm_atomic_and", "imm_atomic_or",
    "imm_atomic_xor", "imm_atomic_exch", "imm_atomic_cmp_exch", "imm_atomic_imax", "imm_atomic_imin",
    "imm_atomic_umax", "imm_atomic_umin", "sync", "dadd", "dmax", "dmin", "dmul", "deq", "dge", "dlt", "dne", "dmov",
    "dmovc", "dtof", "ftod", "eval_snapped", "eval_sample_index", "eval_centroid", "dcl_gsinstances", "abort",
    "debug_break", nullptr, "ddiv", "dfma", "drcp", "msad", "dtoi", "dtou", "itod", "utod", nullptr, "gather4_s",
    "gather4_c_s", "gather4_po_s", "gather4_po_c_s", "ld_s", "ldms_s", "ld_uav_typed_s", "ld_raw_s",
    "ld_structured_s", "sample_l_s", "sample_c_lz_s", "sample_cl_s", "sample_b_cl_s", "sample_d_cl_s",
    "sample_c_cl_s", "check_access_fully_mapped",
};

static const char *register_name(Disasm *d, UINT type)
{
    switch (type) {
    case OPERAND_TEMP:
        return "r";
    case OPERAND_INPUT:
        return "v";
    case OPERAND_OUTPUT:
        return "o";
    case OPERAND_INDEXABLE_TEMP:
        return "x";
    case OPERAND_IMMEDIATE32:
        return "l";
    case OPERAND_IMMEDIATE64:
        return "d";
    case OPERAND_SAMPLER:
        return d->sm51 ? "S" : "s";
    case OPERAND_RESOURCE:
        return d->sm51 ? "T" : "t";
    case OPERAND_CONSTANT_BUFFER:
        return d->sm51 ? "CB" : "cb";
    case OPERAND_IMMEDIATE_CONSTANT_BUFFER:
        return "icb";
    case OPERAND_LABEL:
        return "label";
    case OPERAND_INPUT_PRIMITIVEID:
        return "vPrim";
    case OPERAND_OUTPUT_DEPTH:
        return "oDepth";
    case OPERAND_NULL:
        return "null";
    case OPERAND_RASTERIZER:
        return "rasterizer";
    case OPERAND_OUTPUT_COVERAGE_MASK:
        return "oMask";
    case OPERAND_STREAM:
        return "m";
    case OPERAND_FUNCTION_BODY:
        return "fb";
    case OPERAND_FUNCTION_TABLE:
        return "ft";
    case OPERAND_INTERFACE:
        return "fp";
    case OPERAND_FUNCTION_INPUT:
        return "iv";
    case OPERAND_FUNCTION_OUTPUT:
        return "ov";
    case OPERAND_OUTPUT_CONTROL_POINT_ID:
        return "vOutputControlPointID";
    case OPERAND_INPUT_FORK_INSTANCE_ID:
        return "vForkInstanceID";
    case OPERAND_INPUT_JOIN_INSTANCE_ID:
        return "vJoinInstanceID";
    case OPERAND_INPUT_CONTROL_POINT:
        return "vicp";
    case OPERAND_OUTPUT_CONTROL_POINT:
        return "vocp";
    case OPERAND_INPUT_PATCH_CONSTANT:
        return "vpc";
    case OPERAND_INPUT_DOMAIN_POINT:
        return "vDomain";
    case OPERAND_THIS_POINTER:
        return "this";
    case OPERAND_UNORDERED_ACCESS_VIEW:
        return d->sm51 ? "U" : "u";
    case OPERAND_THREAD_GROUP_SHARED_MEMORY:
        return "g";
    case OPERAND_INPUT_THREAD_ID:
        return "vThreadID";
    case OPERAND_INPUT_THREAD_GROUP_ID:
        return "vThreadGroupID";
    case OPERAND_INPUT_THREAD_ID_IN_GROUP:
        return "vThreadIDInGroup";
    case OPERAND_INPUT_COVERAGE_MASK:
        return "vCoverage";
    case OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED:
        return "vThreadIDInGroupFlattened";
    case OPERAND_INPUT_GS_INSTANCE_ID:
        return "vGSInstanceID";
    case OPERAND_OUTPUT_DEPTH_GREATER_EQUAL:
        return "oDepthGE";
    case OPERAND_OUTPUT_DEPTH_LESS_EQUAL:
        return "oDepthLE";
    case OPERAND_CYCLE_COUNTER:
        return "vCycleCounter";
    case OPERAND_OUTPUT_STENCIL_REF:
        return "oStencilRef";
    case OPERAND_INNER_COVERAGE:
        return "vInnerCoverage";
    }
    return nullptr;
}

/* how literals print: as integers, as floats, or as whichever the bits look like. */
enum { LIT_INT, LIT_FLOAT, LIT_AMBIGUOUS };

static void ambiguous_literal(Disasm *d, UINT v)
{
    UINT exponent = v & 0x7F800000;
    if (v == 0x80000000 || (exponent != 0x7F800000 && exponent)) {
        if (!d->hex) {
            float f;
            memcpy(&f, &v, 4);
            out(d, "%f", (double)f);
            return;
        }
    } else if (!d->hex) {
        int i = (int)v;
        if (i >= -10000 && i <= 10000) {
            out(d, "%d", i);
            return;
        }
    }
    out(d, "0x%08x", v);
}

static void int_literal(Disasm *d, UINT v)
{
    int i = (int)v;
    if (!d->hex && v != 0x80000000 && i >= -10000 && i <= 10000)
        out(d, "%d", i);
    else
        out(d, "0x%08x", v);
}

static constexpr char COMPONENTS[] = "xyzw";

static void min_precision(Disasm *d, UINT value)
{
    if (!value)
        return;
    const char *name = value == 1 ? "min16f" : value == 2 ? "min2_8f" : value == 4 ? "min16i" : value == 5 ? "min16u"
                                                                                                         : "def32";
    out(d, " {%s}", name);
}

/* the bits of the operand token that mark the indices of 1, 2 or 3 dimensions, as fxc brackets them. */
static BOOL bracketed(UINT type) { return type <= 30 && ((0x663801CEu >> type) & 1); }

static void print_operand(Disasm *d, const DxbcOperand *op, int literal)
{
    if (op->modifier == 1 || op->modifier == 3)
        put(d, "-");
    if (op->modifier == 2 || op->modifier == 3)
        put(d, "|");
    const char *name = register_name(d, op->type);
    if (!name) {
        out(d, "<unknown register type %u>", op->type);
        return;
    }
    put(d, name);
    if (op->type == OPERAND_IMMEDIATE32 || op->type == OPERAND_IMMEDIATE64) {
        UINT n = op->components == 4 ? 4 : 1;
        if (op->type == OPERAND_IMMEDIATE64) {
            /* doubles take two words each. */
            double v[2];
            memcpy(&v[0], &op->immediate64[0], 8);
            memcpy(&v[1], &op->immediate64[1], 8);
            if (d->hex)
                out(d, "(0x%08x, 0x%08x)", op->immediate[0], op->immediate[1]);
            else if (n == 1)
                out(d, "(%fl)", v[0]);
            else
                out(d, "(%fl, %fl)", v[0], v[1]);
        } else if (literal == LIT_AMBIGUOUS) {
            put(d, "(");
            for (UINT i = 0; i < n; i++) {
                if (i)
                    put(d, ",");
                ambiguous_literal(d, op->immediate[i]);
            }
            put(d, ")");
        } else if (literal == LIT_FLOAT && !d->hex) {
            float f[4];
            memcpy(f, op->immediate, sizeof(f));
            if (n == 1)
                out(d, "(%f)", (double)f[0]);
            else
                out(d, "(%f, %f, %f, %f)", (double)f[0], (double)f[1], (double)f[2], (double)f[3]);
        } else {
            put(d, "(");
            for (UINT i = 0; i < n; i++) {
                int_literal(d, op->immediate[i]);
                if (i + 1 < n)
                    put(d, ", ");
            }
            put(d, ")");
        }
        min_precision(d, op->min_precision);
    } else {
        for (UINT i = 0; i < op->index_count && i < 3; i++) {
            const DxbcIndex *index = &op->index[i];
            if (!index->relative) {
                BOOL first_plain = i == 0 && (op->index_count == 1 || (op->type != OPERAND_INPUT &&
                                                                       op->type != OPERAND_OUTPUT &&
                                                                       op->type != OPERAND_INPUT_CONTROL_POINT &&
                                                                       op->type != OPERAND_OUTPUT_CONTROL_POINT));
                out(d, !first_plain && bracketed(op->type) ? "[%d]" : "%d", (int)index->immediate);
                continue;
            }
            const DxbcOperand *rel = index->relative;
            put(d, "[");
            const char *rel_name = register_name(d, rel->type);
            put(d, rel_name ? rel_name : "?");
            if (rel->index_count == 2) {
                if (rel->type == OPERAND_INDEXABLE_TEMP)
                    out(d, "%d[%d].", (int)rel->index[0].immediate, (int)rel->index[1].immediate);
                else
                    out(d, "_%d_%d.", (int)rel->index[0].immediate, (int)rel->index[1].immediate);
            } else {
                out(d, "%d.", rel->index_count ? (int)rel->index[0].immediate : 0);
            }
            out(d, "%c", COMPONENTS[rel->swizzle[0] & 3]);
            min_precision(d, rel->min_precision);
            if (rel->nonuniform)
                put(d, " { nonuniform }");
            out(d, " + %d]", (int)index->immediate);
        }
        if (op->components == 4) {
            put(d, ".");
            if (op->selection == SELECT_MASK) {
                for (UINT i = 0; i < 4; i++)
                    if (op->mask & (1 << i))
                        out(d, "%c", COMPONENTS[i]);
            } else if (op->selection == SELECT_SWIZZLE) {
                for (UINT i = 0; i < 4; i++)
                    out(d, "%c", COMPONENTS[op->swizzle[i] & 3]);
            } else {
                out(d, "%c", COMPONENTS[op->swizzle[0] & 3]);
            }
        }
        min_precision(d, op->min_precision);
        if (op->nonuniform)
            put(d, " { nonuniform }");
    }
    if (op->modifier == 2 || op->modifier == 3)
        put(d, "|");
}

/* the literals each instruction reads, as its operands are typed. */
static int literal_kind(UINT opcode, UINT operand)
{
    switch (opcode) {
    case OP_MOV:
    case OP_MOVC:
    case OP_SWAPC:
        return LIT_AMBIGUOUS;
    case OP_STORE_UAV_TYPED:
    case OP_STORE_RAW:
    case OP_STORE_STRUCTURED:
        return operand >= 2 ? LIT_AMBIGUOUS : LIT_INT;
    case OP_ADD:
    case OP_DERIV_RTX:
    case OP_DERIV_RTY:
    case OP_DIV:
    case OP_DP2:
    case OP_DP3:
    case OP_DP4:
    case OP_EQ:
    case OP_EXP:
    case OP_FRC:
    case OP_FTOI:
    case OP_FTOU:
    case OP_GE:
    case OP_LOG:
    case OP_LT:
    case OP_MAD:
    case OP_MIN:
    case OP_MAX:
    case OP_MUL:
    case OP_NE:
    case OP_ROUND_NE:
    case OP_ROUND_NI:
    case OP_ROUND_PI:
    case OP_ROUND_Z:
    case OP_RSQ:
    case OP_SAMPLE:
    case OP_SAMPLE_C:
    case OP_SAMPLE_C_LZ:
    case OP_SAMPLE_L:
    case OP_SAMPLE_D:
    case OP_SAMPLE_B:
    case OP_SQRT:
    case OP_SINCOS:
    case OP_LOD:
    case OP_GATHER4:
    case OP_GATHER4_C:
    case OP_DERIV_RTX_COARSE:
    case OP_DERIV_RTX_FINE:
    case OP_DERIV_RTY_COARSE:
    case OP_DERIV_RTY_FINE:
    case OP_RCP:
    case OP_F32TOF16:
    case OP_FTOD:
    case OP_EVAL_CENTROID:
    case OP_DCL_HS_MAX_TESSFACTOR:
        return LIT_FLOAT;
    case OP_GATHER4_PO:
    case OP_GATHER4_PO_C:
        return operand == 2 ? LIT_INT : LIT_FLOAT;
    }
    return LIT_INT;
}

static const char *resource_dimension(UINT dim)
{
    static const char *const NAMES[] = {"<unknown dimension>", "buffer", "texture1d", "texture2d", "texture2dms",
                                        "texture3d", "texturecube", "texture1darray", "texture2darray",
                                        "texture2dmsarray", "texturecubearray", "raw_buffer", "structured_buffer"};
    return dim < 13 ? NAMES[dim] : NAMES[0];
}

static const char *return_type(UINT type)
{
    static const char *const NAMES[] = {"<unknown resource return type>", "unorm", "snorm", "sint", "uint", "float",
                                        "mixed", "double", "<continued>", "<unused>"};
    return type < 10 ? NAMES[type] : NAMES[0];
}

static void return_types(Disasm *d, UINT token)
{
    out(d, "(%s,%s,%s,%s)", return_type(token & 0xF), return_type((token >> 4) & 0xF), return_type((token >> 8) & 0xF),
        return_type((token >> 12) & 0xF));
}

static const char *system_value_decl(UINT value)
{
    static const char *const NAMES[] = {
        "undefined",       "position",          "clip_distance",     "cull_distance",
        "rendertarget_array_index", "viewport_array_index", "vertex_id", "primitive_id",
        "instance_id",     "is_front_face",     "sampleIndex",       "finalQuadUeq0EdgeTessFactor",
        "finalQuadVeq0EdgeTessFactor", "finalQuadUeq1EdgeTessFactor", "finalQuadVeq1EdgeTessFactor",
        "finalQuadUInsideTessFactor", "finalQuadVInsideTessFactor", "finalTriUeq0EdgeTessFactor",
        "finalTriVeq0EdgeTessFactor", "finalTriWeq0EdgeTessFactor", "finalTriInsideTessFactor",
        "finalLineDetailTessFactor", "finalLineDensityTessFactor",
    };
    return value < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[value] : "undefined";
}

static const char *interpolation(UINT mode)
{
    static const char *const NAMES[] = {" undefined ", " constant ", " linear ", " linear centroid ",
                                        " linear noperspective ", " linear noperspective centroid ",
                                        " linear sample ", " linear noperspective sample "};
    return mode < 8 ? NAMES[mode] : " ??? ";
}

static const char *primitive(UINT value)
{
    static const char *const NAMES[] = {" undefined ", " point ", " line ", " triangle ", " ??? ", " ??? ",
                                        " lineadj ", " triangleadj "};
    static char patch[16];
    if (value < 8)
        return NAMES[value];
    if (value >= 8 && value <= 39) {
        DcText t = {};
        DcTextPrintf(&t, " patch%u ", value - 7);
        if (t.buf && t.length < sizeof(patch))
            memcpy(patch, t.buf, t.length + 1);
        DcTextFree(&t);
        return patch;
    }
    return " ??? ";
}

static void global_flags(Disasm *d, UINT token)
{
    static const char *const FLAGS[] = {"refactoringAllowed", "enableDoublePrecisionFloatOps",
                                        "forceEarlyDepthStencil", "enableRawAndStructuredBuffers", "skipOptimization",
                                        "enableMinimumPrecision", "enable11_1DoubleExtensions",
                                        "enable11_1ShaderExtensions"};
    UINT flags = (token >> 11) & 0xFF;
    put(d, " ");
    if (!flags) {
        put(d, "none");
        return;
    }
    BOOL first = TRUE;
    for (UINT i = 0; i < 8; i++) {
        if (flags & (1 << i)) {
            if (!first)
                put(d, " | ");
            put(d, FLAGS[i]);
            first = FALSE;
        }
    }
}

static void uav_flags(Disasm *d, UINT token)
{
    if (token & (1 << 16))
        put(d, "_glc");
    if (token & (1 << 23))
        put(d, "_opc");
    if (token & (1 << 17))
        put(d, "_rov");
}

static void print_icb(Disasm *d, const DxbcInstruction *ins)
{
    put(d, "dcl_immediateConstantBuffer { ");
    for (UINT i = 0; i + 3 < ins->extra_count; i += 4) {
        if (i)
            put(d, ",\n                              ");
        put(d, "{ ");
        for (UINT j = 0; j < 4; j++) {
            if (j)
                put(d, ", ");
            ambiguous_literal(d, ins->extra[i + j]);
        }
        put(d, "}");
    }
    put(d, " }");
}

static void print_declaration(Disasm *d, const DxbcInstruction *ins)
{
    UINT op = ins->opcode, token = ins->token;
    const DxbcOperand *operand = ins->operand_count ? &ins->operands[0] : nullptr;
    put(d, OPCODE_NAMES[op]);
    switch (op) {
    case OP_DCL_RESOURCE:
        out(d, "_%s", resource_dimension(TOK_RESOURCE_DIMENSION(token)));
        if (TOK_RESOURCE_DIMENSION(token) == RES_TEXTURE2DMS || TOK_RESOURCE_DIMENSION(token) == RES_TEXTURE2DMSARRAY)
            out(d, "(%d)", TOK_RESOURCE_SAMPLES(token));
        put(d, " ");
        if (ins->extra_count)
            return_types(d, ins->extra[0]);
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        break;
    case OP_DCL_UAV_TYPED:
        out(d, "_%s", resource_dimension(TOK_RESOURCE_DIMENSION(token)));
        uav_flags(d, token);
        put(d, " ");
        if (ins->extra_count)
            return_types(d, ins->extra[0]);
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        break;
    case OP_DCL_UAV_RAW:
    case OP_DCL_UAV_STRUCTURED:
        uav_flags(d, token);
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        if (op == OP_DCL_UAV_STRUCTURED && ins->extra_count)
            out(d, ", %u", ins->extra[0]);
        break;
    case OP_DCL_RESOURCE_RAW:
    case OP_DCL_RESOURCE_STRUCTURED:
    case OP_DCL_TGSM_RAW:
    case OP_DCL_TGSM_STRUCTURED:
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        for (UINT i = 0; i < ins->extra_count && i < 2; i++)
            out(d, ", %u", ins->extra[i]);
        break;
    case OP_DCL_CONSTANT_BUFFER:
        /* the buffer is named in capitals and without components, its size after its range in 5.1. */
        if (d->sm51 && operand && operand->index_count >= 3) {
            out(d, " CB%d[%d:", operand->index[0].immediate, operand->index[1].immediate);
            if (operand->index[2].immediate == 0xFFFFFFFF)
                put(d, "*]");
            else
                out(d, "%d]", operand->index[2].immediate);
            out(d, "[%d]", ins->extra_count ? ins->extra[0] : 0);
        } else if (operand) {
            out(d, " CB%d[%d]", operand->index[0].immediate, operand->index_count > 1 ? operand->index[1].immediate : 0);
        }
        put(d, TOK_CB_DYNAMIC(token) ? ", dynamicIndexed" : ", immediateIndexed");
        if (d->sm51)
            out(d, ", space=%d", ins->extra_count > 1 ? ins->extra[1] : 0);
        break;
    case OP_DCL_SAMPLER: {
        static const char *const MODES[] = {", mode_default", ", mode_comparison", ", mode_mono"};
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        UINT mode = TOK_SAMPLER_MODE(token);
        put(d, mode < 3 ? MODES[mode] : ", mode_default");
        break;
    }
    case OP_DCL_INPUT_PS:
    case OP_DCL_INPUT_PS_SIV:
        put(d, interpolation(TOK_INTERPOLATION(token)));
        print_operand(d, operand, LIT_INT);
        if (op == OP_DCL_INPUT_PS_SIV && ins->extra_count)
            out(d, ", %s", system_value_decl(ins->extra[0]));
        break;
    case OP_DCL_INPUT_PS_SGV:
        if (TOK_INTERPOLATION(token))
            put(d, interpolation(TOK_INTERPOLATION(token)));
        else
            put(d, " ");
        print_operand(d, operand, LIT_INT);
        if (ins->extra_count)
            out(d, ", %s", system_value_decl(ins->extra[0]));
        break;
    case OP_DCL_INPUT_SGV:
    case OP_DCL_INPUT_SIV:
    case OP_DCL_OUTPUT_SGV:
    case OP_DCL_OUTPUT_SIV:
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        if (ins->extra_count)
            out(d, ", %s", system_value_decl(ins->extra[0]));
        break;
    case OP_DCL_INPUT:
    case OP_DCL_OUTPUT:
    case OP_DCL_STREAM:
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        break;
    case OP_DCL_INDEX_RANGE:
        put(d, " ");
        print_operand(d, operand, LIT_INT);
        if (ins->extra_count)
            out(d, " %d", ins->extra[0]);
        break;
    case OP_DCL_TEMPS:
    case OP_DCL_MAX_OUTPUT_VERTEX_COUNT:
    case OP_DCL_INPUT_CONTROL_POINT_COUNT:
    case OP_DCL_OUTPUT_CONTROL_POINT_COUNT:
    case OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT:
    case OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT:
    case OP_DCL_GS_INSTANCE_COUNT:
        if (op == OP_DCL_INPUT_CONTROL_POINT_COUNT || op == OP_DCL_OUTPUT_CONTROL_POINT_COUNT)
            out(d, " %u", (token >> 11) & 0x3F);
        else if (ins->extra_count)
            out(d, " %u", ins->extra[0]);
        break;
    case OP_DCL_INDEXABLE_TEMP:
        if (ins->extra_count >= 3)
            out(d, " x%i[%i], %d", ins->extra[0], ins->extra[1], ins->extra[2]);
        break;
    case OP_DCL_GLOBAL_FLAGS:
        global_flags(d, token);
        break;
    case OP_DCL_GS_INPUT_PRIMITIVE:
        put(d, primitive((token >> 11) & 0x3F));
        break;
    case OP_DCL_GS_OUTPUT_TOPOLOGY: {
        static const char *const TOPOLOGIES[] = {" undefined ", " pointlist ", " ??? ", " linestrip ", " ??? ",
                                                 " trianglestrip "};
        UINT topology = (token >> 11) & 0x7F;
        put(d, topology < 6 ? TOPOLOGIES[topology] : " ??? ");
        break;
    }
    case OP_DCL_TESS_DOMAIN: {
        static const char *const DOMAINS[] = {" ???", " domain_isoline", " domain_tri", " domain_quad"};
        put(d, DOMAINS[(token >> 11) & 3]);
        break;
    }
    case OP_DCL_TESS_PARTITIONING: {
        static const char *const MODES[] = {" ???", " partitioning_integer", " partitioning_pow2",
                                            " partitioning_fractional_odd", " partitioning_fractional_even"};
        UINT mode = (token >> 11) & 7;
        put(d, mode < 5 ? MODES[mode] : " ???");
        break;
    }
    case OP_DCL_TESS_OUTPUT_PRIMITIVE: {
        static const char *const PRIMITIVES[] = {" ???", " output_point", " output_line", " output_triangle_cw",
                                                 " output_triangle_ccw"};
        UINT primitive_kind = (token >> 11) & 7;
        put(d, primitive_kind < 5 ? PRIMITIVES[primitive_kind] : " ???");
        break;
    }
    case OP_DCL_HS_MAX_TESSFACTOR:
        if (ins->extra_count) {
            float f;
            memcpy(&f, &ins->extra[0], 4);
            out(d, " l(%f)", (double)f);
        }
        break;
    case OP_DCL_THREAD_GROUP:
        if (ins->extra_count >= 3)
            out(d, " %u, %u, %u", ins->extra[0], ins->extra[1], ins->extra[2]);
        break;
    case OP_DCL_FUNCTION_BODY:
        if (ins->extra_count)
            out(d, " fb%u", ins->extra[0]);
        break;
    case OP_DCL_FUNCTION_TABLE:
        if (ins->extra_count >= 2) {
            out(d, " ft%u = {", ins->extra[0]);
            for (UINT i = 0; i < ins->extra[1] && i + 2 < ins->extra_count; i++)
                out(d, i ? ", fb%u" : "fb%u", ins->extra[2 + i]);
            put(d, "}");
        }
        break;
    case OP_DCL_INTERFACE:
        if (token & (1 << 11))
            put(d, "_dynamicindexed");
        if (ins->extra_count >= 3) {
            UINT tables = ins->extra[2] & 0xFFFF, arrays = ins->extra[2] >> 16;
            out(d, " fp%u[%u][%u] = {", ins->extra[0], arrays, ins->extra[1]);
            for (UINT i = 0; i < tables && i + 3 < ins->extra_count; i++)
                out(d, i ? ", ft%u" : "ft%u", ins->extra[3 + i]);
            put(d, "}");
        }
        break;
    default:
        put(d, " ");
        break;
    }
}

static BOOL conditional(UINT opcode)
{
    return opcode == OP_IF || opcode == OP_BREAKC || opcode == OP_CONTINUEC || opcode == OP_RETC ||
           opcode == OP_DISCARD || opcode == OP_CALLC;
}

static BOOL samples_resource(UINT opcode)
{
    switch (opcode) {
    case OP_LD:
    case OP_LD_MS:
    case OP_SAMPLE:
    case OP_SAMPLE_C:
    case OP_SAMPLE_C_LZ:
    case OP_SAMPLE_L:
    case OP_SAMPLE_D:
    case OP_SAMPLE_B:
    case OP_GATHER4:
    case OP_GATHER4_C:
    case OP_GATHER4_PO:
    case OP_GATHER4_PO_C:
    case OP_LOD:
    case OP_RESINFO:
    case OP_SAMPLE_INFO:
    case OP_SAMPLE_POS:
    case OP_LD_UAV_TYPED:
    case OP_LD_RAW:
    case OP_LD_STRUCTURED:
    case OP_BUFINFO:
        return TRUE;
    }
    return opcode >= OP_GATHER4_FEEDBACK && opcode <= OP_SAMPLE_C_CLAMP_FEEDBACK;
}

static void print_instruction(Disasm *d, const DxbcInstruction *ins)
{
    UINT op = ins->opcode, token = ins->token;
    if (op == OP_ELSE || op == OP_ENDIF || op == OP_ENDLOOP || op == OP_ENDSWITCH)
        d->indent = d->indent ? d->indent - 1 : 0;
    for (UINT i = 0; i < d->indent; i++)
        put(d, "  ");
    if (op == OP_CUSTOMDATA) {
        if (TOK_CUSTOMDATA_CLASS(token) == 3)
            print_icb(d, ins);
        else
            put(d, "// undecipherable custom data");
        put(d, "\n");
        return;
    }
    if (DcIsDeclaration(op) && op != OP_HS_DECLS && op != OP_HS_CONTROL_POINT_PHASE && op != OP_HS_FORK_PHASE &&
        op != OP_HS_JOIN_PHASE) {
        print_declaration(d, ins);
        put(d, "\n");
        return;
    }
    const char *name = op < 235 ? OPCODE_NAMES[op] : nullptr;
    out(d, "%s", name ? name : "<unknown opcode>");
    BOOL offsets = ins->offset[0] || ins->offset[1] || ins->offset[2];
    if (offsets)
        put(d, "_aoffimmi");
    if (ins->resource_dimension && samples_resource(op))
        put(d, "_indexable");
    if (ins->saturate)
        put(d, "_sat");
    if (ins->precise) {
        put(d, " [precise");
        if (ins->precise != 0xF) {
            put(d, "(");
            for (UINT i = 0; i < 4; i++)
                if (ins->precise & (1 << i))
                    out(d, "%c", COMPONENTS[i]);
            put(d, ")");
        }
        put(d, "]");
    }
    if (offsets)
        out(d, "(%i,%i,%i)", ins->offset[0], ins->offset[1], ins->offset[2]);
    if (ins->resource_dimension && samples_resource(op)) {
        out(d, "(%s", resource_dimension(ins->resource_dimension));
        if (ins->resource_stride)
            out(d, ", stride=%u", ins->resource_stride);
        put(d, ")");
        if (ins->return_type[0] || ins->return_type[1] || ins->return_type[2] || ins->return_type[3])
            return_types(d, ins->return_type[0] | ins->return_type[1] << 4 | ins->return_type[2] << 8 |
                                ins->return_type[3] << 12);
    }
    if (op == OP_SYNC) {
        UINT flags = TOK_SYNC_FLAGS(token);
        if (flags & SYNC_UAV_GLOBAL)
            put(d, "_uglobal");
        if (flags & SYNC_UAV_GROUP)
            put(d, "_ugroup");
        if (flags & SYNC_GROUP_SHARED_MEMORY)
            put(d, "_g");
        if (flags & SYNC_THREADS_IN_GROUP)
            put(d, "_t");
    }
    if (conditional(op))
        put(d, ins->nonzero ? "_nz " : "_z ");
    else if (op == OP_RESINFO && TOK_RESINFO_RETURN(token) == 2)
        put(d, "_uint ");
    else if (op == OP_RESINFO && TOK_RESINFO_RETURN(token) == 1)
        put(d, "_rcpfloat ");
    else if (op == OP_SAMPLE_INFO && TOK_RESINFO_RETURN(token) == 1)
        put(d, "_uint ");
    else
        put(d, " ");
    if (op == OP_STORE_UAV_TYPED || op == OP_STORE_RAW || op == OP_STORE_STRUCTURED || op == OP_LD_UAV_TYPED)
        uav_flags(d, token & ~(1u << 23));
    for (UINT i = 0; i < ins->operand_count; i++) {
        if (i)
            put(d, ", ");
        print_operand(d, &ins->operands[i], literal_kind(op, i));
    }
    put(d, "\n");
    if (op == OP_IF || op == OP_ELSE || op == OP_LOOP || op == OP_SWITCH)
        d->indent++;
}

/* ------------------------------------------------------------------ the entry points */

/* what numbering counts: not declarations, but the phase markers and shader messages. */
static BOOL executable(const DxbcInstruction *ins)
{
    if (ins->opcode == OP_CUSTOMDATA)
        return TOK_CUSTOMDATA_CLASS(ins->token) == 4;
    if (ins->opcode == OP_HS_CONTROL_POINT_PHASE || ins->opcode == OP_HS_FORK_PHASE || ins->opcode == OP_HS_JOIN_PHASE)
        return TRUE;
    return !DcIsDeclaration(ins->opcode);
}

static HRESULT disassemble(LPCVOID data, SIZE_T size, UINT flags, LPCSTR comments, SIZE_T start, SIZE_T count,
                           SIZE_T *finish, ID3DBlob **out_blob)
{
    if (!data || size > 0xFFFFFFFF || !out_blob || (flags & ~0xF7u))
        return D3DERR_INVALIDCALL;
    *out_blob = nullptr;
    if (start >= size)
        return E_INVALIDARG;
    DcContainer c;
    HRESULT hr = DcOpenContainer(data, size, &c);
    if (hr == D3DERR_INVALIDCALL)
        /* legacy direct3d 9 bytecode and effects are not disassembled. */
        return E_NOTIMPL;
    if (FAILED(hr))
        return hr;
    int index = DcFindChunk(&c, DC_TAG('S', 'H', 'E', 'X'));
    if (index < 0)
        index = DcFindChunk(&c, DC_TAG('S', 'H', 'D', 'R'));
    if (index < 0)
        return E_FAIL;
    DcChunk code = DcGetChunk(&c, index);
    if (code.size < 8)
        return E_FAIL;
    UINT version = read32(code.data);
    Disasm d;
    memset(&d, 0, sizeof(d));
    d.flags = flags;
    d.hex = (flags & D3D_DISASM_PRINT_HEX_LITERALS) != 0;
    UINT major = (version >> 4) & 0xF, minor = version & 0xF;
    d.sm51 = major > 5 || (major == 5 && minor > 0);
    DcReflection *reflection = nullptr;
    ID3D11ShaderReflection *r = nullptr;
    if (SUCCEEDED(DcReflectionCreate(&c, TRUE, &reflection)))
        r = DcReflectionInterface(reflection);
    if (r && !(flags & D3D_DISASM_INSTRUCTION_ONLY))
        print_header(&d, &c, r, comments);
    static const char *const PROGRAMS[] = {"ps", "vs", "gs", "hs", "ds", "cs"};
    UINT program = version >> 16;
    const char *program_name = program < 6 ? PROGRAMS[program] : program == 0xFFF0 ? "lib" : nullptr;
    if (!program_name) {
        hr = E_FAIL;
        goto done;
    }
    BOOL numbering = (flags & D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING) != 0;
    BOOL offsets = (flags & D3D_DISASM_ENABLE_INSTRUCTION_OFFSET) != 0;
    SIZE_T lines = 0;
    if (start < 8 && count) {
        if (numbering)
            put(&d, "      ");
        if (offsets)
            put(&d, "0x00000000: ");
        out(&d, "%s_%d_%d\n", program_name, major, minor);
        lines = 1;
    }
    UINT executed = 0;
    UINT words = read32(code.data + 4);
    if (words > code.size / 4)
        words = code.size / 4;
    DxbcReader reader = {(const uint32_t *)code.data, words, 2};
    UINT next = 8;
    while (lines < count) {
        UINT at = reader.at * 4;
        DxbcInstruction ins;
        int result = dxbc_read(&reader, &ins);
        if (result <= 0) {
            if (result < 0)
                hr = E_FAIL;
            break;
        }
        next = reader.at * 4;
        BOOL counted = executable(&ins);
        if (at < start) {
            executed += counted;
            continue;
        }
        if (numbering && counted)
            out(&d, "%4u%c ", executed, offsets ? ' ' : ':');
        else if (numbering)
            put(&d, "      ");
        if (offsets)
            out(&d, "0x%08X: ", at);
        print_instruction(&d, &ins);
        executed += counted;
        lines++;
    }
    if (finish)
        *finish = next;
    if (SUCCEEDED(hr) && r && !(flags & D3D_DISASM_INSTRUCTION_ONLY)) {
        D3D11_SHADER_DESC desc;
        r->lpVtbl->GetDesc(r, &desc);
        out(&d, "// Approximately %i instruction slots used\n", desc.InstructionCount);
    }
    if (SUCCEEDED(hr))
        hr = d.text.failed ? E_OUTOFMEMORY : DcTextBlob(d.text.buf ? d.text.buf : "", d.text.length, out_blob);
done:
    if (r)
        r->lpVtbl->Release(r);
    DcTextFree(&d.text);
    return hr;
}

D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble(LPCVOID data, SIZE_T size, UINT flags, LPCSTR comments, ID3DBlob **out)
{
    return disassemble(data, size, flags, comments, 0, (SIZE_T)-1, nullptr, out);
}

D3DCOMPILERAPI HRESULT WINAPI D3DDisassembleRegion(LPCVOID data, SIZE_T size, UINT flags, LPCSTR comments,
                                                   SIZE_T start, SIZE_T count, SIZE_T *finish, ID3DBlob **out)
{
    return disassemble(data, size, flags, comments, start, count, finish, out);
}
