/*
 * the direct3d 8 shader and declaration validators. errors are kept as
 * text when asked, as the exported validators return it.
 */
#include "d3d8_private.h"

typedef struct Log {
    BOOL keep;
    char *text;
    UINT length, capacity;
    UINT errors;
} Log;

static void error(Log *log, const char *format, ...)
{
    log->errors++;
    if (!log->keep)
        return;
    char line[256];
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(line, sizeof(line) - 1, format, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(line) - 2)
        n = sizeof(line) - 2;
    line[n++] = '\n';
    if (log->length + n + 1 > log->capacity) {
        UINT capacity = D8_MAX(log->capacity * 2, log->length + n + 256);
        char *text = log->text ? HeapReAlloc(GetProcessHeap(), 0, log->text, capacity)
                               : HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!text)
            return;
        log->text = text;
        log->capacity = capacity;
    }
    memcpy(log->text + log->length, line, n);
    log->length += n;
    log->text[log->length] = 0;
}

/* the error text goes in a process heap buffer the caller frees. */
static HRESULT finish(Log *log, char **errors)
{
    if (errors) {
        *errors = HeapAlloc(GetProcessHeap(), 0, log->length + 1);
        if (*errors) {
            if (log->length)
                memcpy(*errors, log->text, log->length);
            (*errors)[log->length] = 0;
        } else {
            OutputDebugStringA("Out of memory.\n");
        }
    }
    HeapFree(GetProcessHeap(), 0, log->text);
    return log->errors ? E_FAIL : D3D_OK;
}

#define REG_TYPE(t) ((((t) >> 28) & 7))
#define REG_NUM(t) ((t) & 0x1FFF)
#define WRITE_MASK(t) (((t) >> 16) & 0xF)
#define SRC_MOD(t) (((t) >> 24) & 0xF)
#define SWIZZLE(t) (((t) >> 16) & 0xFF)
#define IDENTITY_SWIZZLE 0xE4

enum { RT_TEMP, RT_INPUT, RT_CONST, RT_ADDR, RT_RASTOUT, RT_ATTROUT, RT_TEXCRDOUT };
#define RT_TEXTURE RT_ADDR

static BOOL comment(DWORD token) { return (token & 0xFFFF) == 0xFFFE && !(token & 0x80000000); }

/* ------------------------------------------------------------------ declarations */

/* returns the registers the declaration feeds, by bit. inputs is 16 for a shader, 17 for fixed function. */
static DWORD validate_declaration(Log *log, const DWORD *decl, const D3DCAPS8 *caps, UINT inputs, UINT constants)
{
    DWORD declared = 0, streams = 0, normal_sources = 0, normal_targets = 0;
    BOOL in_stream = FALSE, tess = FALSE, tess_seen = FALSE, data_seen = TRUE, fixed = inputs == 17;
    for (const DWORD *p = decl; *p != 0xFFFFFFFF; p++) {
        DWORD token = *p;
        UINT kind = token >> 29;
        switch (kind) {
        case D3DVSD_TOKEN_NOP:
            break;
        case D3DVSD_TOKEN_STREAM:
            if (token & (1u << 28)) {
                if (tess_seen) {
                    error(log, "(Validation Error) Declaration Token #1: Only one tessellator stream may be declared.");
                    return declared;
                }
                if (token & 0xF)
                    error(log, "(Validation Error) Declaration Token #1: Tessellator stream number must be 0.");
                tess = tess_seen = TRUE;
            } else {
                UINT n = token & 0xF;
                if (caps && n >= caps->MaxStreams) {
                    error(log, "(Validation Error) Declaration Token #1: Stream number %d is too high.", n);
                    return declared;
                }
                if (streams & (1u << n)) {
                    error(log, "(Validation Error) Declaration Token #1: Stream %d is declared twice.", n);
                    return declared;
                }
                streams |= 1u << n;
                tess = FALSE;
            }
            in_stream = TRUE;
            data_seen = FALSE;
            break;
        case D3DVSD_TOKEN_STREAMDATA: {
            if (!in_stream || tess) {
                error(log, "(Validation Error) Declaration Token #1: Stream data without a stream selector.");
                return declared;
            }
            data_seen = TRUE;
            if (token & (1u << 28)) {
                if (fixed)
                    error(log, "(Validation Error) Declaration Token #1: SKIP is not allowed in a fixed function "
                               "declaration.");
                break;
            }
            UINT reg = token & 0x1F, type = (token >> 16) & 0xF;
            if (reg >= inputs)
                error(log, "(Validation Error) Declaration Token #1: Register v%d is out of range.", reg);
            if (type > D3DVSDT_SHORT4)
                error(log, "(Validation Error) Declaration Token #1: Unknown data type %d.", type);
            if (reg < 32 && (declared & (1u << reg)))
                error(log, "(Validation Error) Declaration Token #1: Register v%d specified twice in the declaration.",
                      reg);
            if (reg < 32)
                declared |= 1u << reg;
            break;
        }
        case D3DVSD_TOKEN_TESSELLATOR: {
            if (!tess) {
                error(log, "(Validation Error) Declaration Token #1: Tessellator data outside a tessellator stream.");
                return declared;
            }
            data_seen = TRUE;
            UINT out = token & 0x1F, in = (token >> 20) & 0xF, type = (token >> 16) & 0xF;
            if (out >= inputs)
                error(log, "(Validation Error) Declaration Token #1: Register v%d is out of range.", out);
            if (token & (1u << 28)) {
                if (type != D3DVSDT_FLOAT2 || in || (out < 32 && (declared & (1u << out))))
                    error(log, "(Validation Error) Declaration Token #1: Bad texture coordinate generation.");
            } else {
                if (in >= inputs || (out < 32 && (normal_targets & (1u << out))))
                    error(log, "(Validation Error) Declaration Token #1: Bad normal generation.");
                normal_sources |= 1u << in;
                if (out < 32)
                    normal_targets |= 1u << out;
            }
            if (out < 32)
                declared |= 1u << out;
            break;
        }
        case D3DVSD_TOKEN_CONSTMEM: {
            if (!data_seen)
                error(log, "(Validation Error) Declaration Token #1: Stream selector token must be followed by at "
                           "least one stream data definition token.");
            UINT start = token & 0x7F, count = (token >> 25) & 0xF;
            if (constants && start + count > constants)
                error(log, "(Validation Error) Declaration Token #1: Constants c%d to c%d are out of range.", start,
                      start + count - 1);
            p += 4 * count;
            in_stream = FALSE;
            data_seen = TRUE;
            break;
        }
        case D3DVSD_TOKEN_EXT:
            if (!data_seen)
                error(log, "(Validation Error) Declaration Token #1: Stream selector token must be followed by at "
                           "least one stream data definition token.");
            p += (token >> 24) & 0x1F;
            in_stream = FALSE;
            data_seen = TRUE;
            break;
        default:
            error(log, "(Validation Error) Declaration Token #1: Unrecognized stream declaration token.");
            return declared;
        }
    }
    if (normal_sources & ~declared)
        error(log, "(Global Validation Error) Normal generation reads a register the declaration does not define.");
    return declared;
}

/* ------------------------------------------------------------------ vertex shaders */

typedef struct Op {
    WORD opcode;
    BYTE dst, src, slots;
} Op;

static const Op VS_OPS[] = {
    {0, 0, 0, 0},  {1, 1, 1, 1},  {2, 1, 2, 1},  {4, 1, 3, 1},  {5, 1, 2, 1},  {6, 1, 1, 1},  {7, 1, 1, 1},
    {8, 1, 2, 1},  {9, 1, 2, 1},  {10, 1, 2, 1}, {11, 1, 2, 1}, {12, 1, 2, 1}, {13, 1, 2, 1}, {14, 1, 1, 10},
    {15, 1, 1, 10}, {16, 1, 1, 1}, {17, 1, 2, 1}, {19, 1, 1, 3}, {20, 1, 2, 4}, {21, 1, 2, 3}, {22, 1, 2, 4},
    {23, 1, 2, 3}, {24, 1, 2, 2}, {78, 1, 1, 1}, {79, 1, 1, 1},
};

static const Op *find_op(const Op *ops, UINT count, DWORD opcode)
{
    for (UINT i = 0; i < count; i++)
        if (ops[i].opcode == opcode)
            return &ops[i];
    return nullptr;
}

static UINT matrix_rows(DWORD opcode)
{
    switch (opcode) {
    case 20:
    case 22:
        return 4;
    case 21:
    case 23:
        return 3;
    case 24:
        return 2;
    default:
        return 0;
    }
}

static void validate_vertex_code(Log *log, const DWORD *code, const D3DCAPS8 *caps, DWORD declared)
{
    DWORD version = code[0];
    if ((version >> 16) != 0xFFFE || ((version & 0xFFFF) != 0x0100 && (version & 0xFFFF) != 0x0101)) {
        error(log, "(Validation Error) Version Token: Unsupported vertex shader version %08x.", version);
        return;
    }
    UINT minor = version & 0xFF;
    if (caps && (version & 0xFFFF) > (caps->VertexShaderVersion & 0xFFFF)) {
        error(log, "(Validation Error) Version Token: Version too high for device.");
        return;
    }
    UINT constants = caps ? caps->MaxVertexShaderConst : 0;
    UINT slots = 0, statement = 0;
    DWORD written_out = 0;
    for (UINT at = 1;; statement++) {
        DWORD token = code[at];
        if (token == 0x0000FFFF)
            break;
        if (comment(token)) {
            at += 1 + ((token >> 16) & 0x7FFF);
            continue;
        }
        if (token & 0xFFFF0000) {
            error(log, "(Statement %d) (Validation Error) Reserved bits set in the instruction token.", statement);
            return;
        }
        const Op *op = find_op(VS_OPS, RTL_NUMBER_OF(VS_OPS), token);
        if (!op) {
            error(log, "(Statement %d) (Validation Error) Unrecognized instruction.", statement);
            return;
        }
        slots += op->slots;
        at++;
        if (op->dst) {
            DWORD dst = code[at++];
            UINT type = REG_TYPE(dst), num = REG_NUM(dst);
            if (!(dst & 0x80000000) || (dst & 0x0FF0E000)) {
                error(log, "(Statement %d) (Validation Error) Bad destination parameter token.", statement);
                return;
            }
            if (!WRITE_MASK(dst))
                error(log, "(Statement %d) (Validation Error) Empty write mask.", statement);
            switch (type) {
            case RT_TEMP:
                if (num >= 12)
                    error(log, "(Statement %d) (Validation Error) Temporary register r%d is out of range.", statement,
                          num);
                break;
            case RT_ADDR:
                if (minor < 1 || token != 1 || num || WRITE_MASK(dst) != 1)
                    error(log, "(Statement %d) (Validation Error) Only mov may write a0.x, in vs.1.1.", statement);
                break;
            case RT_RASTOUT:
                if (num >= 3)
                    error(log, "(Statement %d) (Validation Error) Bad rasterizer output register.", statement);
                else if (!num)
                    written_out |= WRITE_MASK(dst);
                break;
            case RT_ATTROUT:
                if (num >= 2)
                    error(log, "(Statement %d) (Validation Error) Bad color output register.", statement);
                break;
            case RT_TEXCRDOUT:
                if (num >= 8)
                    error(log, "(Statement %d) (Validation Error) Bad texture coordinate output register.",
                          statement);
                break;
            default:
                error(log, "(Statement %d) (Validation Error) Invalid destination register type.", statement);
                break;
            }
            if (token == 19 && WRITE_MASK(dst) != 2 && WRITE_MASK(dst) != 3)
                error(log, "(Statement %d) (Validation Error) frc may only write .y or .xy.", statement);
        }
        BOOL absolute_const = FALSE, relative_const = FALSE;
        for (UINT i = 0; i < op->src; i++) {
            DWORD src = code[at++];
            UINT type = REG_TYPE(src), num = REG_NUM(src);
            BOOL relative = (src >> 13) & 1;
            if (!(src & 0x80000000) || (src & 0x40000000) || (src & 0xC000)) {
                error(log, "(Statement %d) (Validation Error) Bad source parameter token.", statement);
                return;
            }
            if (SRC_MOD(src) > 1)
                error(log, "(Statement %d) (Validation Error) Only negation may modify a source.", statement);
            UINT rows = i == 1 ? matrix_rows(token) : 0;
            if (rows && (SRC_MOD(src) || SWIZZLE(src) != IDENTITY_SWIZZLE))
                error(log, "(Statement %d) (Validation Error) A matrix source cannot be negated or swizzled.",
                      statement);
            UINT span = rows ? rows : 1;
            switch (type) {
            case RT_TEMP:
                if (relative || num + span > 12)
                    error(log, "(Statement %d) (Validation Error) Bad temporary register.", statement);
                break;
            case RT_INPUT:
                if (relative || num + span > 16)
                    error(log, "(Statement %d) (Validation Error) Bad input register.", statement);
                else if (declared != ~0u && (~declared & (((1u << span) - 1) << num)))
                    error(log, "(Statement %d) (Validation Error) Input register v%d is not declared.", statement,
                          num);
                break;
            case RT_CONST:
                if (relative) {
                    if (minor < 1)
                        error(log, "(Statement %d) (Validation Error) Relative addressing needs vs.1.1.", statement);
                    relative_const = TRUE;
                } else {
                    if (constants && num + span > constants)
                        error(log, "(Statement %d) (Validation Error) Constant register c%d is out of range.",
                              statement, num);
                    absolute_const = TRUE;
                }
                break;
            default:
                error(log, "(Statement %d) (Validation Error) Invalid source register type.", statement);
                break;
            }
        }
        if (absolute_const && relative_const)
            error(log, "(Statement %d) (Validation Error) Absolute and relative constant reads cannot mix.",
                  statement);
        /* operands past what the instruction takes are left as they are. */
        while (code[at] & 0x80000000)
            at++;
    }
    if (slots > 128)
        error(log, "(Global Validation Error) Too many instruction slots: %d of 128.", slots);
    if ((written_out & 3) != 3)
        error(log, "(Global Validation Error) oPos.x and oPos.y must be written.");
}

HRESULT D8ValidateVertexShader(const DWORD *code, const DWORD *declaration, const D3DCAPS8 *caps, BOOL text,
                               char **errors)
{
    Log log = {.keep = text};
    if (!code && !declaration) {
        error(&log, "Version Token: Code and declaration pointers passed into shader vertex shader validator cannot "
                    "both be NULL.");
        return finish(&log, errors);
    }
    UINT constants = caps ? caps->MaxVertexShaderConst : 0;
    DWORD declared = declaration ? validate_declaration(&log, declaration, caps, code ? 16 : 17, constants) : ~0u;
    if (code && !log.errors)
        validate_vertex_code(&log, code, caps, declared);
    return finish(&log, errors);
}

/* ------------------------------------------------------------------ pixel shaders */

enum { TEX = 1, ARITH = 2, DEF = 4 };

typedef struct PsOp {
    WORD opcode;
    BYTE dst, src;
    BYTE kind;
    BYTE min_minor;
} PsOp;

/* the sources of each instruction before ps.1.4, and the version that added it. */
static const PsOp PS_OPS[] = {
    {0, 0, 0, 0, 0},     {1, 1, 1, ARITH, 0}, {2, 1, 2, ARITH, 0}, {3, 1, 2, ARITH, 0}, {4, 1, 3, ARITH, 0},
    {5, 1, 2, ARITH, 0}, {8, 1, 2, ARITH, 0}, {9, 1, 2, ARITH, 2}, {18, 1, 3, ARITH, 0}, {64, 1, 0, TEX, 0},
    {65, 1, 0, TEX, 0},  {66, 1, 0, TEX, 0},  {67, 1, 1, TEX, 0},  {68, 1, 1, TEX, 0},  {69, 1, 1, TEX, 0},
    {70, 1, 1, TEX, 0},  {71, 1, 1, TEX, 0},  {72, 1, 1, TEX, 0},  {73, 1, 1, TEX, 0},  {74, 1, 1, TEX, 0},
    {76, 1, 2, TEX, 0},  {77, 1, 1, TEX, 0},  {80, 1, 3, ARITH, 0}, {81, 1, 0, DEF, 0}, {82, 1, 1, TEX, 2},
    {83, 1, 1, TEX, 2},  {84, 1, 1, TEX, 3},  {85, 1, 1, TEX, 2},  {86, 1, 1, TEX, 2},  {88, 1, 3, ARITH, 2},
};

static const PsOp PS14_OPS[] = {
    {0, 0, 0, 0, 4},      {1, 1, 1, ARITH, 4},  {2, 1, 2, ARITH, 4},  {3, 1, 2, ARITH, 4},  {4, 1, 3, ARITH, 4},
    {5, 1, 2, ARITH, 4},  {8, 1, 2, ARITH, 4},  {9, 1, 2, ARITH, 4},  {18, 1, 3, ARITH, 4}, {64, 1, 1, TEX, 4},
    {65, 1, 0, TEX, 4},   {66, 1, 1, TEX, 4},   {80, 1, 3, ARITH, 4}, {81, 1, 0, DEF, 4},   {87, 1, 0, TEX, 4},
    {88, 1, 3, ARITH, 4}, {89, 1, 2, ARITH, 4}, {0xFFFD, 0, 0, 0, 4},
};

static const PsOp *find_ps_op(BOOL v14, DWORD opcode, UINT minor)
{
    const PsOp *ops = v14 ? PS14_OPS : PS_OPS;
    UINT count = v14 ? RTL_NUMBER_OF(PS14_OPS) : RTL_NUMBER_OF(PS_OPS);
    for (UINT i = 0; i < count; i++)
        if (ops[i].opcode == opcode && minor >= ops[i].min_minor)
            return &ops[i];
    return nullptr;
}

static BOOL valid_ps_register(UINT type, UINT num, UINT minor, BOOL dst, BOOL def)
{
    BOOL v14 = minor >= 4;
    switch (type) {
    case RT_TEMP:
        return num < (v14 ? 6u : 2u);
    case RT_INPUT:
        return !dst && num < 2;
    case RT_CONST:
        return num < 8 && (!dst || def);
    case RT_TEXTURE:
        return num < (v14 ? 6u : 4u) && (!dst || !v14);
    default:
        return FALSE;
    }
}

/* checks the slots of a shader or of a ps.1.4 phase, counting arithmetic and texture apart. */
static void check_slots(Log *log, UINT minor, UINT tex, UINT arith, UINT total)
{
    UINT max_tex = minor >= 4 ? 6 : 4, max_total = minor >= 4 ? 14 : minor == 0 ? 8 : 12;
    if (tex > max_tex || arith > 8 || total > max_total)
        error(log, "(Global Validation Error) Too many instructions.");
}

HRESULT D8ValidatePixelShader(const DWORD *code, const D3DCAPS8 *caps, BOOL text, char **errors)
{
    Log log = {.keep = text};
    if (!code) {
        OutputDebugStringA("Out of memory.\n");
        if (errors)
            *errors = nullptr;
        return E_FAIL;
    }
    DWORD version = code[0];
    UINT minor = version & 0xFF;
    if ((version >> 16) != 0xFFFF || (version & 0xFF00) != 0x0100 || minor > 4) {
        error(&log, "(Validation Error) Version Token: Unsupported pixel shader version %08x.", version);
        return finish(&log, errors);
    }
    if (caps && (version & 0xFFFF) > (caps->PixelShaderVersion & 0xFFFF)) {
        error(&log, "(Validation Error) Version Token: Version too high for device.");
        return finish(&log, errors);
    }
    BOOL v14 = minor >= 4, arith_seen = FALSE, other_seen = FALSE, r0_written = FALSE, phase = FALSE;
    UINT tex = 0, arith = 0, total = 0, statement = 0;
    for (UINT at = 1;; statement++) {
        DWORD token = code[at];
        if (token == 0x0000FFFF)
            break;
        if (comment(token)) {
            at += 1 + ((token >> 16) & 0x7FFF);
            continue;
        }
        if (token & 0xBFFF0000) {
            error(&log, "(Statement %d) (Validation Error) Reserved bits set in the instruction token.", statement);
            break;
        }
        BOOL coissue = (token >> 30) & 1;
        DWORD opcode = token & 0xFFFF;
        const PsOp *op = find_ps_op(v14, opcode, minor);
        if (!op) {
            error(&log, "(Statement %d) (Validation Error) Unrecognized instruction.", statement);
            break;
        }
        at++;
        if (op->kind == DEF) {
            if (other_seen)
                error(&log, "(Statement %d) (Validation Error) def must come before all other instructions.",
                      statement);
            DWORD dst = code[at];
            if (REG_TYPE(dst) != RT_CONST || REG_NUM(dst) >= 8 || WRITE_MASK(dst) != 0xF || (dst & 0x0FF00000))
                error(&log, "(Statement %d) (Validation Error) def needs a full constant register c0 to c7.",
                      statement);
            at += 5;
            continue;
        }
        if (opcode == 0xFFFD) {
            if (phase)
                error(&log, "(Statement %d) (Validation Error) Only one phase marker is allowed.", statement);
            check_slots(&log, minor, tex, arith, total);
            phase = TRUE;
            arith_seen = FALSE;
            tex = arith = total = 0;
            other_seen = TRUE;
            continue;
        }
        if (opcode)
            other_seen = TRUE;
        if (op->kind == TEX) {
            if (coissue)
                error(&log, "(Statement %d) (Validation Error) Texture instructions cannot be co-issued.", statement);
            if (arith_seen)
                error(&log, "(Statement %d) (Validation Error) Texture instructions must come before arithmetic "
                            "ones.",
                      statement);
            tex += !v14 && minor == 0 && (opcode == 67 || opcode == 68) ? 2 : 1;
            if (!v14 && opcode == 68)
                arith++;
            if (v14 || minor > 0 || opcode != 66)
                total++;
        } else if (op->kind == ARITH) {
            arith_seen = TRUE;
            if (!coissue) {
                arith += v14 && opcode == 89 ? 2 : 1;
                total++;
            }
        }
        if (op->dst) {
            DWORD dst = code[at++];
            UINT type = REG_TYPE(dst), num = REG_NUM(dst);
            if (!(dst & 0x80000000) || (dst & 0x4000E000)) {
                error(&log, "(Statement %d) (Validation Error) Bad destination parameter token.", statement);
                break;
            }
            /* texkill names the register it tests, which ps.1.4 lets be a texture coordinate. */
            BOOL ok = opcode == 65 ? valid_ps_register(type, num, minor, FALSE, FALSE) && type != RT_INPUT &&
                                         type != RT_CONST
                                   : valid_ps_register(type, num, minor, TRUE, FALSE);
            if (!ok)
                error(&log, "(Statement %d) (Validation Error) Invalid destination register.", statement);
            if (!WRITE_MASK(dst))
                error(&log, "(Statement %d) (Validation Error) Empty write mask.", statement);
            if (type == RT_TEMP && !num)
                r0_written = TRUE;
        }
        for (UINT i = 0; i < op->src; i++) {
            DWORD src = code[at++];
            if (!(src & 0x80000000) || (src & 0x4000E000)) {
                error(&log, "(Statement %d) (Validation Error) Bad source parameter token.", statement);
                return finish(&log, errors);
            }
            if (!valid_ps_register(REG_TYPE(src), REG_NUM(src), minor, FALSE, FALSE))
                error(&log, "(Statement %d) (Validation Error) Invalid source register.", statement);
            if (SRC_MOD(src) > 10)
                error(&log, "(Statement %d) (Validation Error) Invalid source modifier.", statement);
        }
        while (code[at] & 0x80000000)
            at++;
    }
    check_slots(&log, minor, tex, arith, total);
    if (!r0_written)
        error(&log, "(Global Validation Error) r0 must be written by the shader.");
    return finish(&log, errors);
}
