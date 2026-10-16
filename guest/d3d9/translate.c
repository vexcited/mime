/*
 * direct3d 9's shader bytecode as shader model 5 dxbc. a d3d9 temp r#
 * stays r#, constants come from constant buffers (floats cb0, integers
 * and booleans cb1, what d3d9.dll adds cb2), samplers s# are t# and s#.
 * outputs are written to temps and copied at the end, where the vertex
 * shader moves the position by half a pixel, as direct3d 9's pixel
 * centers are at integers and direct3d 10's at halves, and the pixel shader applies fog and the
 * alpha test of the device's state, as fixed function did after shaders
 * of model 2 and older. interpolated values sit in the registers of
 * their semantics, the same for every shader (D9VaryingSlot), so any
 * vertex shader links with any pixel shader.
 *
 * the bytecode's format: an instruction token holds the opcode in bits
 * 0 to 15, a control field in 16 to 23 (comparisons, texld's kind), the
 * length in 24 to 27 from model 2 on, predication in bit 28 and co-issue
 * in 30. a parameter token holds the register number in bits 0 to 10 and
 * its type in 28 to 30 and 11 to 12; a destination's write mask in 16 to
 * 19, its modifiers in 20 to 23 and its shift in 24 to 27; a source's
 * swizzle in 16 to 23 and its modifier in 24 to 27, bit 13 relative
 * addressing, followed by the address register's token from model 2 on.
 */
#include "d3d9_private.h"
#include "../lib/dxbc/tokens.h"

#define SWZ_XYZW 0xE4
#define SWZ(x, y, z, w) ((x) | (y) << 2 | (z) << 4 | (w) << 6)
#define REPL(c) SWZ(c, c, c, c)

/* the register types of parameter tokens. */
enum {
    RT_TEMP = 0,
    RT_INPUT = 1,
    RT_CONST = 2,
    RT_ADDR = 3,
    RT_TEXTURE = 3,
    RT_RASTOUT = 4,
    RT_ATTROUT = 5,
    RT_TEXCRDOUT = 6,
    RT_OUTPUT = 6,
    RT_CONSTINT = 7,
    RT_COLOROUT = 8,
    RT_DEPTHOUT = 9,
    RT_SAMPLER = 10,
    RT_CONST2 = 11,
    RT_CONST3 = 12,
    RT_CONST4 = 13,
    RT_CONSTBOOL = 14,
    RT_LOOP = 15,
    RT_TEMPFLOAT16 = 16,
    RT_MISCTYPE = 17,
    RT_LABEL = 18,
    RT_PREDICATE = 19,
};

/* source modifiers. */
enum {
    SM_NONE,
    SM_NEG,
    SM_BIAS,
    SM_BIASNEG,
    SM_SIGN,
    SM_SIGNNEG,
    SM_COMP,
    SM_X2,
    SM_X2NEG,
    SM_DZ,
    SM_DW,
    SM_ABS,
    SM_ABSNEG,
    SM_NOT,
};

UINT D9RegType(DWORD token) { return ((token >> 28) & 7) | ((token >> 8) & 0x18); }
UINT D9RegNum(DWORD token) { return token & 0x7FF; }

/* ------------------------------------------------------------------ interpolated values */

static const char *const USAGES[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL",   "PSIZE",
                                     "TEXCOORD", "TANGENT",     "BINORMAL",     "TESSFACTOR", "POSITIONT",
                                     "COLOR",    "FOG",         "DEPTH",        "SAMPLE"};

const char *D9UsageName(UINT usage) { return usage < RTL_NUMBER_OF(USAGES) ? USAGES[usage] : "TEXCOORD"; }

static SRWLOCK g_slot_lock = SRWLOCK_INIT;
static WORD g_slots[D9_SLOT_POSITION];
static UINT g_slot_count;

/*
 * texture coordinates 0 to 7 are registers 0 to 7, the colors 8 and 9,
 * fog 10; other semantics take the next free register the first time a
 * shader of the process uses them, as few programs use many.
 */
int D9VaryingSlot(UINT usage, UINT index)
{
    if (usage == D3DDECLUSAGE_TEXCOORD && index < 8)
        return index;
    if (usage == D3DDECLUSAGE_COLOR && index < 2)
        return 8 + index;
    if (usage == D3DDECLUSAGE_FOG && !index)
        return 10;
    WORD key = (WORD)(usage << 8 | index | 0x8000);
    int slot = -1;
    AcquireSRWLockExclusive(&g_slot_lock);
    for (UINT i = 0; i < g_slot_count; i++)
        if (g_slots[i] == key)
            slot = 11 + i;
    if (slot < 0 && 11 + g_slot_count < D9_SLOT_POSITION) {
        g_slots[g_slot_count] = key;
        slot = 11 + g_slot_count++;
    }
    ReleaseSRWLockExclusive(&g_slot_lock);
    return slot;
}

/* ------------------------------------------------------------------ parsing */

/* the source parameters of each opcode of model 1, whose instructions have no length. */
static int sm1_sources(UINT op, BOOL pixel, UINT major, UINT minor, BOOL *dst)
{
    *dst = TRUE;
    switch (op) {
    case D9_OP_NOP:
    case D9_OP_RET:
    case D9_OP_ENDLOOP:
    case D9_OP_ENDREP:
    case D9_OP_ELSE:
    case D9_OP_ENDIF:
    case D9_OP_BREAK:
    case D9_OP_PHASE:
        *dst = FALSE;
        return 0;
    case D9_OP_CALL:
    case D9_OP_LABEL:
    case D9_OP_REP:
    case D9_OP_IF:
    case D9_OP_BREAKP:
        *dst = FALSE;
        return 1;
    case D9_OP_CALLNZ:
    case D9_OP_LOOP:
    case D9_OP_IFC:
    case D9_OP_BREAKC:
        *dst = FALSE;
        return 2;
    case D9_OP_MOV:
    case D9_OP_RCP:
    case D9_OP_RSQ:
    case D9_OP_EXP:
    case D9_OP_LOG:
    case D9_OP_LIT:
    case D9_OP_FRC:
    case D9_OP_ABS:
    case D9_OP_NRM:
    case D9_OP_MOVA:
    case D9_OP_EXPP:
    case D9_OP_LOGP:
    case D9_OP_DSX:
    case D9_OP_DSY:
    case D9_OP_TEXBEM:
    case D9_OP_TEXBEML:
    case D9_OP_TEXREG2AR:
    case D9_OP_TEXREG2GB:
    case D9_OP_TEXREG2RGB:
    case D9_OP_TEXM3x2PAD:
    case D9_OP_TEXM3x2TEX:
    case D9_OP_TEXM3x3PAD:
    case D9_OP_TEXM3x3TEX:
    case D9_OP_TEXM3x3VSPEC:
    case D9_OP_TEXDP3TEX:
    case D9_OP_TEXM3x2DEPTH:
    case D9_OP_TEXDP3:
    case D9_OP_TEXM3x3:
        return 1;
    case D9_OP_SINCOS:
        return major >= 3 ? 1 : 3;
    case D9_OP_ADD:
    case D9_OP_SUB:
    case D9_OP_MUL:
    case D9_OP_DP3:
    case D9_OP_DP4:
    case D9_OP_MIN:
    case D9_OP_MAX:
    case D9_OP_SLT:
    case D9_OP_SGE:
    case D9_OP_DST:
    case D9_OP_M4x4:
    case D9_OP_M4x3:
    case D9_OP_M3x4:
    case D9_OP_M3x3:
    case D9_OP_M3x2:
    case D9_OP_POW:
    case D9_OP_CRS:
    case D9_OP_BEM:
    case D9_OP_SETP:
    case D9_OP_TEXLDL:
    case D9_OP_TEXM3x3SPEC:
        return 2;
    case D9_OP_MAD:
    case D9_OP_LRP:
    case D9_OP_SGN:
    case D9_OP_CND:
    case D9_OP_CMP:
    case D9_OP_DP2ADD:
        return 3;
    case D9_OP_TEXLDD:
        return 4;
    case D9_OP_TEXCOORD:
        return pixel && major == 1 && minor >= 4 ? 1 : 0;
    case D9_OP_TEX:
        if (pixel && major == 1)
            return minor >= 4 ? 1 : 0;
        return 2;
    case D9_OP_TEXKILL:
    case D9_OP_TEXDEPTH:
        return 0;
    default:
        return -1;
    }
}

/* the parameters of the instruction at code: destination, predicate, sources, and the words it takes. */
BOOL D9Decode(const D9ShaderInfo *info, const DWORD *code, UINT left, D9Inst *inst)
{
    memset(inst, 0, sizeof(*inst));
    if (!left)
        return FALSE;
    DWORD token = code[0];
    inst->opcode = token & 0xFFFF;
    inst->control = (token >> 16) & 0xFF;
    inst->predicated = (token >> 28) & 1;
    inst->coissue = (token >> 30) & 1;
    if (inst->opcode == D9_OP_COMMENT) {
        inst->words = 1 + ((token >> 16) & 0x7FFF);
        return inst->words <= left;
    }
    if (inst->opcode == D9_OP_END) {
        inst->words = 1;
        return TRUE;
    }
    UINT at = 1;
    BOOL relative_token = info->major >= 2;
    if (inst->opcode == D9_OP_DCL) {
        /* the usage or sampler type, then the register. */
        if (left < 3)
            return FALSE;
        inst->usage = code[1];
        inst->dst.token = code[2];
        inst->has_dst = TRUE;
        inst->words = 3;
        return TRUE;
    }
    if (inst->opcode == D9_OP_DEF || inst->opcode == D9_OP_DEFI || inst->opcode == D9_OP_DEFB) {
        UINT n = inst->opcode == D9_OP_DEFB ? 1 : 4;
        if (left < 2 + n)
            return FALSE;
        inst->dst.token = code[1];
        inst->has_dst = TRUE;
        memcpy(inst->values, code + 2, n * 4);
        inst->words = 2 + n;
        return TRUE;
    }
    BOOL has_dst;
    int sources = sm1_sources(inst->opcode, info->pixel, info->major, info->minor, &has_dst);
    if (sources < 0)
        return FALSE;
    UINT length = info->major >= 2 ? (token >> 24) & 0xF : 0;
    if (inst->opcode == D9_OP_TEXKILL || inst->opcode == D9_OP_TEXDEPTH ||
        (inst->opcode == D9_OP_TEXCOORD && !sources) || (inst->opcode == D9_OP_TEX && !sources))
        has_dst = TRUE;
    if (has_dst) {
        if (at >= left)
            return FALSE;
        inst->dst.token = code[at++];
        inst->has_dst = TRUE;
        if (relative_token && (inst->dst.token & (1u << 13)) && at < left)
            inst->dst.rel = code[at++], inst->dst.has_rel = TRUE;
    }
    if (inst->predicated) {
        if (at >= left)
            return FALSE;
        inst->pred.token = code[at++];
    }
    for (int i = 0; i < sources && i < 4; i++) {
        if (at >= left)
            return FALSE;
        inst->src[i].token = code[at++];
        if (relative_token && (inst->src[i].token & (1u << 13)) && at < left)
            inst->src[i].rel = code[at++], inst->src[i].has_rel = TRUE;
        else if (!relative_token && (inst->src[i].token & (1u << 13)))
            inst->src[i].has_rel = TRUE;
    }
    inst->src_count = sources;
    inst->words = at;
    /* the length the bytecode gives wins: it covers what a decoder of these versions does not know. */
    if (length && length + 1 != at)
        inst->words = length + 1;
    return inst->words <= left;
}

/* ------------------------------------------------------------------ translation */

typedef struct Tx {
    const D9ShaderInfo *info;
    const D9ShaderKey *key;
    D9Code code;
    D9Signature inputs, outputs;
    UINT next_temp;
    UINT temp_max;
    /* internal temps: the address register, predicates, the outputs' copies, scratch. */
    UINT r_addr, r_pred;
    /* the rows texm3x2 and texm3x3 instructions compute, and the next one's place. */
    UINT r_matrix, matrix_row;
    UINT r_out[16];
    /* vertex shaders: the inputs of integer attributes, turned to floats. */
    UINT r_in[16];
    UINT loop_temps[8];
    BOOL loop_kinds[8];
    UINT loop_steps[8];
    UINT loop_depth;
    /* the samplers read, and their dimensions: 2 2d, 3 cube, 4 volume, with 0x80 those that compare. */
    UINT samplers_used;
    BYTE sampler_dims[20];
    /* the registers declared: outputs of vertex shaders, inputs of pixel shaders, and theirs. */
    UINT outputs_written, inputs_read, centroid;
    BOOL writes_size, reads_position, reads_point_coord;
    BOOL in_main;
    BOOL failed;
    const char *why;
} Tx;

static UINT temp(Tx *t)
{
    UINT r = t->next_temp++;
    if (t->next_temp > t->temp_max)
        t->temp_max = t->next_temp;
    return r;
}

static void fail(Tx *t, const char *why)
{
    if (!t->failed)
        t->why = why;
    t->failed = TRUE;
}

static DWORD fbits(float f)
{
    DWORD v;
    memcpy(&v, &f, 4);
    return v;
}

/* the swizzle of a d3d9 source, d3d9's order being dxbc's. */
static UINT src_swizzle(DWORD token) { return (token >> 16) & 0xFF; }

static UINT dst_mask(DWORD token) { return (token >> 16) & 0xF; }

/* a swizzle restricted to a write mask's components, as instructions of fewer components read. */
static UINT first_component(UINT mask)
{
    for (UINT i = 0; i < 4; i++)
        if (mask & (1u << i))
            return i;
    return 0;
}

static D9Opnd texcoord_input(Tx *t, UINT n, UINT swizzle);
static UINT treg(UINT n);

/* the index register of a relative source: a0's or aL's component, as an integer temp. */
static int relative_reg(Tx *t, const D9Param *p, UINT *component)
{
    if (!p->has_rel)
        return -1;
    if (t->info->major < 2) {
        *component = 0;
        return (int)t->r_addr;
    }
    UINT type = D9RegType(p->rel);
    *component = (p->rel >> 16) & 3;
    if (type == RT_LOOP) {
        *component = 0;
        return t->loop_depth ? (int)t->loop_temps[t->loop_depth - 1] : (int)t->r_addr;
    }
    return (int)t->r_addr;
}

/* the dxbc register of a pixel shader input of model 2 or 3. */
static int ps_input_reg(Tx *t, UINT type, UINT num)
{
    const D9ShaderInfo *info = t->info;
    if (info->major >= 3)
        return num < 16 ? D9VaryingSlot(info->in_usage[num], info->in_index[num]) : -1;
    if (type == RT_INPUT)
        return num < 2 ? D9VaryingSlot(D3DDECLUSAGE_COLOR, num) : -1;
    return num < 8 ? D9VaryingSlot(D3DDECLUSAGE_TEXCOORD, num) : -1;
}

/* a source register as an operand, without its modifier. */
static D9Opnd source_reg(Tx *t, const D9Param *p, UINT swizzle, UINT offset)
{
    DWORD token = p->token;
    UINT type = D9RegType(token), num = D9RegNum(token) + offset;
    UINT component = 0;
    switch (type) {
    case RT_TEMP:
    case RT_TEMPFLOAT16:
        return D9TempSrc(num, swizzle);
    case RT_INPUT:
        if (t->info->pixel) {
            int reg = ps_input_reg(t, type, num);
            if (reg >= 0 && reg < 8 && t->key->point_sprite)
                return D9Src(OPERAND_INPUT, D9_SLOT_POINT_COORD, swizzle);
            if (reg < 0 || !(t->key->ps_inputs & (1u << reg)))
                return D9Imm4(0, 0, 0, 0);
            return D9Src(OPERAND_INPUT, reg, swizzle);
        }
        if (num < 16 && (t->key->int_inputs >> (num * 2) & 3))
            return D9TempSrc(t->r_in[num], swizzle);
        return D9Src(OPERAND_INPUT, num, swizzle);
    case RT_TEXTURE:
        if (t->info->pixel && t->info->major < 2) {
            if (t->info->minor >= 4)
                return texcoord_input(t, num, swizzle);
            return D9TempSrc(treg(num), swizzle);
        }
        if (t->info->pixel && t->key->point_sprite)
            return D9Src(OPERAND_INPUT, D9_SLOT_POINT_COORD, swizzle);
        if (t->info->pixel) {
            int reg = ps_input_reg(t, type, num);
            if (reg < 0 || !(t->key->ps_inputs & (1u << reg)))
                return D9Imm4(0, 0, 0, 0);
            return D9Src(OPERAND_INPUT, reg, swizzle);
        }
        return D9TempSrc(t->r_addr, swizzle);
    case RT_CONST:
    case RT_CONST2:
    case RT_CONST3:
    case RT_CONST4: {
        UINT base = type == RT_CONST2 ? 2048 : type == RT_CONST3 ? 4096 : type == RT_CONST4 ? 6144 : 0;
        int rel = relative_reg(t, p, &component);
        if (t->info->pixel && t->info->major < 2) {
            /* model 1's constants are within [-1, 1]. */
            UINT r = temp(t);
            D9Op2(&t->code, OP_MAX, D9Temp(r, 0xF), D9Cb(0, base + num, swizzle, rel, component), D9ImmF(-1.0f));
            D9Op2(&t->code, OP_MIN, D9Temp(r, 0xF), D9TempSrc(r, SWZ_XYZW), D9ImmF(1.0f));
            return D9TempSrc(r, SWZ_XYZW);
        }
        return D9Cb(0, base + num, swizzle, rel, component);
    }
    case RT_CONSTINT:
        return D9Cb(1, num, swizzle, -1, 0);
    case RT_CONSTBOOL:
        return D9Cb(1, 16 + num / 4, REPL(num % 4), -1, 0);
    case RT_LOOP:
        return D9TempSrc(t->loop_depth ? t->loop_temps[t->loop_depth - 1] : t->r_addr, REPL(0));
    case RT_PREDICATE:
        return D9TempSrc(t->r_pred, swizzle);
    case RT_MISCTYPE:
        if (num == 0)
            return D9Src(OPERAND_TEMP, t->r_out[15], swizzle);
        return D9TempSrc(t->r_out[14], swizzle);
    default:
        fail(t, "a source register of an unknown type");
        return D9TempSrc(0, swizzle);
    }
}

/* a source with its modifier applied: negations and absolutes in the operand, others computed into a temp. */
static D9Opnd source_at(Tx *t, const D9Param *p, UINT offset)
{
    UINT swizzle = src_swizzle(p->token);
    UINT modifier = (p->token >> 24) & 0xF;
    D9Opnd o = source_reg(t, p, swizzle, offset);
    switch (modifier) {
    case SM_NONE:
        return o;
    case SM_NEG:
        return D9Neg(o);
    case SM_ABS:
        return D9Abs(o);
    case SM_ABSNEG:
        return D9Neg(D9Abs(o));
    case SM_NOT: {
        UINT r = temp(t);
        D9Op1(&t->code, OP_NOT, D9Temp(r, 0xF), o);
        return D9TempSrc(r, SWZ_XYZW);
    }
    default: {
        UINT r = temp(t);
        D9Opnd d = D9Temp(r, 0xF);
        switch (modifier) {
        case SM_BIAS:
        case SM_BIASNEG:
            D9Op2(&t->code, OP_ADD, d, o, D9ImmF(-0.5f));
            break;
        case SM_SIGN:
        case SM_SIGNNEG:
            D9Op3(&t->code, OP_MAD, d, o, D9ImmF(2.0f), D9ImmF(-1.0f));
            break;
        case SM_COMP:
            D9Op2(&t->code, OP_ADD, d, D9Neg(o), D9ImmF(1.0f));
            break;
        case SM_X2:
        case SM_X2NEG:
            D9Op2(&t->code, OP_ADD, d, o, o);
            break;
        case SM_DZ:
        case SM_DW: {
            D9Opnd by = o;
            by.words[0] = (by.words[0] & ~(0xFFu << 4)) | REPL(modifier == SM_DZ ? 2 : 3) << 4;
            D9Op2(&t->code, OP_DIV, d, o, by);
            break;
        }
        default:
            fail(t, "a source modifier of an unknown kind");
            break;
        }
        D9Opnd r_src = D9TempSrc(r, SWZ_XYZW);
        return modifier == SM_BIASNEG || modifier == SM_SIGNNEG || modifier == SM_X2NEG ? D9Neg(r_src) : r_src;
    }
    }
}

static D9Opnd source(Tx *t, const D9Param *p) { return source_at(t, p, 0); }

/* the temp a destination register is written to: d3d9's temps are dxbc's, outputs their copies. */
static BOOL dest_temp(Tx *t, const D9Param *p, UINT *reg)
{
    DWORD token = p->token;
    UINT type = D9RegType(token), num = D9RegNum(token);
    const D9ShaderInfo *info = t->info;
    switch (type) {
    case RT_TEMP:
    case RT_TEMPFLOAT16:
        *reg = num;
        return TRUE;
    case RT_ADDR:
        if (info->pixel) {
            /* ps 1.4 and earlier write texture registers: temps of their own. */
            *reg = 32 + num;
            return TRUE;
        }
        *reg = t->r_addr;
        return TRUE;
    case RT_RASTOUT:
        if (num > 2)
            break;
        *reg = t->r_out[num];
        return TRUE;
    case RT_ATTROUT:
        if (num > 1)
            break;
        *reg = t->r_out[3 + num];
        return TRUE;
    case RT_TEXCRDOUT:
        if (info->major >= 3) {
            if (num >= 12)
                break;
            *reg = t->r_out[num];
            return TRUE;
        }
        if (num > 7)
            break;
        *reg = t->r_out[5 + num];
        return TRUE;
    case RT_COLOROUT:
        if (num > 3)
            break;
        *reg = t->r_out[num];
        return TRUE;
    case RT_DEPTHOUT:
        *reg = t->r_out[4];
        return TRUE;
    case RT_PREDICATE:
        *reg = t->r_pred;
        return TRUE;
    default:
        break;
    }
    fail(t, "a destination register of an unknown type");
    *reg = 0;
    return FALSE;
}

/*
 * writes what the instruction computed into scratch to its destination:
 * shifted, saturated, and only where the predicate holds.
 */
static void write_dest(Tx *t, const D9Inst *inst, UINT scratch, UINT mask)
{
    UINT reg;
    if (!dest_temp(t, &inst->dst, &reg))
        return;
    DWORD token = inst->dst.token;
    BOOL saturate = (token >> 20) & 1;
    int shift = (int)((token >> 24) & 0xF);
    if (shift > 7)
        shift -= 16;
    D9Opnd value = D9TempSrc(scratch, SWZ_XYZW);
    if (shift) {
        float scale = shift > 0 ? (float)(1 << shift) : 1.0f / (float)(1 << -shift);
        D9Op2(&t->code, OP_MUL, D9Temp(scratch, mask), value, D9ImmF(scale));
    }
    if (inst->predicated) {
        D9Opnd pred = D9TempSrc(t->r_pred, src_swizzle(inst->pred.token));
        BOOL negate = ((inst->pred.token >> 24) & 0xF) == SM_NOT;
        D9Opnd keep = D9TempSrc(reg, SWZ_XYZW);
        D9Op3(&t->code, OP_MOVC | (DWORD)saturate << 13, D9Temp(reg, mask), pred, negate ? keep : value,
              negate ? value : keep);
        return;
    }
    D9Op1(&t->code, OP_MOV | (DWORD)saturate << 13, D9Temp(reg, mask), value);
}

/* the opcodes of instructions dxbc has alike: their operands are d3d9's. */
static DWORD simple_opcode(UINT op)
{
    switch (op) {
    case D9_OP_MOV:
        return OP_MOV;
    case D9_OP_ADD:
        return OP_ADD;
    case D9_OP_MAD:
        return OP_MAD;
    case D9_OP_MUL:
        return OP_MUL;
    case D9_OP_DP3:
        return OP_DP3;
    case D9_OP_DP4:
        return OP_DP4;
    case D9_OP_MIN:
        return OP_MIN;
    case D9_OP_MAX:
        return OP_MAX;
    case D9_OP_FRC:
        return OP_FRC;
    case D9_OP_DSX:
        return OP_DERIV_RTX;
    case D9_OP_DSY:
        return OP_DERIV_RTY;
    default:
        return 0;
    }
}

/* ifc, breakc and setp's comparisons: 1 greater, 2 equal, 3 greater or equal, 4 less, 5 not equal, 6 less or equal. */
static void compare(Tx *t, D9Opnd dst, UINT kind, D9Opnd a, D9Opnd b)
{
    switch (kind) {
    case 1:
        D9Op2(&t->code, OP_LT, dst, b, a);
        break;
    case 2:
        D9Op2(&t->code, OP_EQ, dst, a, b);
        break;
    case 3:
        D9Op2(&t->code, OP_GE, dst, a, b);
        break;
    case 4:
        D9Op2(&t->code, OP_LT, dst, a, b);
        break;
    case 5:
        D9Op2(&t->code, OP_NE, dst, a, b);
        break;
    case 6:
        D9Op2(&t->code, OP_GE, dst, b, a);
        break;
    default:
        fail(t, "a comparison of an unknown kind");
        break;
    }
}

/* a matrix multiply: rows of dot products of the source with consecutive registers. */
static void matrix(Tx *t, const D9Inst *inst, UINT scratch, UINT rows, UINT columns)
{
    D9Opnd a = source(t, &inst->src[0]);
    for (UINT i = 0; i < rows; i++)
        D9Op2(&t->code, columns == 4 ? OP_DP4 : OP_DP3, D9Temp(scratch, 1u << i), a, source_at(t, &inst->src[1], i));
}

/* texture sampling: texld and its kinds, texldl and texldd, with the sampler's type and shadow comparisons. */
static void sample(Tx *t, const D9Inst *inst, UINT scratch, const D9Param *coord_param, UINT sampler,
                   UINT result_swizzle)
{
    const D9ShaderInfo *info = t->info;
    const D9ShaderKey *key = t->key;
    UINT unit = sampler;
    UINT type = sampler < 16 ? info->sampler_type[sampler] : 2;
    if (!type)
        type = key->sampler_types >> (sampler * 2) & 3 ? 2 + (key->sampler_types >> (sampler * 2) & 3) : 2;
    BOOL shadow = info->pixel && (key->shadow & (1u << sampler)) && type == 2;
    D9Opnd coord = source(t, coord_param);
    UINT op = inst->opcode;
    BOOL project = op == D9_OP_TEX && inst->control == 1;
    BOOL bias = op == D9_OP_TEX && inst->control == 2;
    UINT c = temp(t);
    D9Op1(&t->code, OP_MOV, D9Temp(c, 0xF), coord);
    if (project)
        D9Op2(&t->code, OP_DIV, D9Temp(c, 0xF), D9TempSrc(c, SWZ_XYZW), D9TempSrc(c, REPL(3)));
    D9Opnd cs = D9TempSrc(c, SWZ_XYZW);
    D9Opnd res = D9ResourceOpnd(unit), smp = D9SamplerOpnd(unit);
    D9Opnd out = D9Temp(scratch, 0xF);
    D9Opnd ops[6] = {out, cs, res, smp};
    if (shadow) {
        D9Opnd ref = D9TempScalar(c, 2);
        D9Opnd shadow_ops[5] = {D9Temp(scratch, 1), cs, D9Src(OPERAND_RESOURCE, unit, REPL(0)), smp, ref};
        D9Emit(&t->code, op == D9_OP_TEXLDL ? OP_SAMPLE_C_LZ : OP_SAMPLE_C, shadow_ops, 5);
        D9Op1(&t->code, OP_MOV, out, D9TempSrc(scratch, REPL(0)));
    } else if (op == D9_OP_TEXLDL) {
        ops[4] = D9TempScalar(c, 3);
        D9Emit(&t->code, OP_SAMPLE_L, ops, 5);
    } else if (op == D9_OP_TEXLDD) {
        ops[4] = source(t, &inst->src[2]);
        ops[5] = source(t, &inst->src[3]);
        D9Emit(&t->code, OP_SAMPLE_D, ops, 6);
    } else if (bias) {
        ops[4] = D9TempScalar(c, 3);
        D9Emit(&t->code, OP_SAMPLE_B, ops, 5);
    } else if (!info->pixel) {
        ops[4] = D9ImmF(0.0f);
        D9Emit(&t->code, OP_SAMPLE_L, ops, 5);
    } else {
        D9Emit(&t->code, OP_SAMPLE, ops, 4);
    }
    if (result_swizzle != SWZ_XYZW)
        D9Op1(&t->code, OP_MOV, out, D9TempSrc(scratch, result_swizzle));
    t->samplers_used |= 1u << unit;
    t->sampler_dims[unit] = shadow ? 2 | 0x80 : type;
}

/* ------------------------------------------------------------------ pixel shaders of model 1 */

/* a stage's texture coordinates, as interpolated, or the point's. */
static D9Opnd texcoord_input(Tx *t, UINT n, UINT swizzle)
{
    if (t->key->point_sprite)
        return D9Src(OPERAND_INPUT, D9_SLOT_POINT_COORD, swizzle);
    if (n >= 8 || !(t->key->ps_inputs & (1u << n)))
        return D9Imm4(0, 0, 0, 0);
    return D9Src(OPERAND_INPUT, n, swizzle);
}

/* a sample of a stage's texture at coordinates, projected when asked, into scratch. */
static void sample_stage(Tx *t, UINT unit, D9Opnd coords, BOOL project, UINT scratch)
{
    const D9ShaderKey *key = t->key;
    UINT kind = key->sampler_types >> (unit * 2) & 3;
    UINT type = kind ? 2 + kind : 2;
    BOOL shadow = (key->shadow & (1u << unit)) && type == 2;
    UINT c = temp(t);
    D9Op1(&t->code, OP_MOV, D9Temp(c, 0xF), coords);
    if (project)
        D9Op2(&t->code, OP_DIV, D9Temp(c, 0xF), D9TempSrc(c, SWZ_XYZW), D9TempSrc(c, REPL(3)));
    D9Opnd smp = D9SamplerOpnd(unit);
    if (shadow) {
        D9Opnd ops[5] = {D9Temp(scratch, 1), D9TempSrc(c, SWZ_XYZW), D9Src(OPERAND_RESOURCE, unit, REPL(0)), smp,
                         D9TempScalar(c, 2)};
        D9Emit(&t->code, OP_SAMPLE_C, ops, 5);
        D9Op1(&t->code, OP_MOV, D9Temp(scratch, 0xF), D9TempSrc(scratch, REPL(0)));
    } else {
        D9Opnd ops[4] = {D9Temp(scratch, 0xF), D9TempSrc(c, SWZ_XYZW), D9ResourceOpnd(unit), smp};
        D9Emit(&t->code, OP_SAMPLE, ops, 4);
    }
    t->samplers_used |= 1u << unit;
    t->sampler_dims[unit] = shadow ? 2 | 0x80 : type;
}

/* the texture register t# of models 1.0 to 1.3, a temp of its own. */
static UINT treg(UINT n) { return 32 + n; }

/* the bump matrix and luminance of a stage, in cb2 from 4 and 12. */
static D9Opnd bump(UINT stage, UINT swizzle) { return D9Cb(2, 4 + stage, swizzle, -1, 0); }

/*
 * the instructions of models 1.0 to 1.4 that read textures. t# of 1.0 to
 * 1.3 holds the stage's sample or coordinates once an instruction wrote
 * it; 1.4's texld samples the stage of its destination at coordinates of
 * t# or r#, divided by z or w with _dz and _dw.
 */
static BOOL ps1_texture(Tx *t, const D9Inst *inst)
{
    const D9ShaderInfo *info = t->info;
    D9Code *c = &t->code;
    UINT op = inst->opcode;
    UINT m = D9RegNum(inst->dst.token);
    BOOL v14 = info->minor >= 4;
    UINT scratch = temp(t);
    switch (op) {
    case D9_OP_TEX:
        if (v14) {
            /* texld r#, t# or r#: the coordinates, divided by z or w with _dz and _dw. */
            const D9Param *p = &inst->src[0];
            UINT modifier = (p->token >> 24) & 0xF;
            D9Opnd coords = D9RegType(p->token) == RT_TEXTURE ? texcoord_input(t, D9RegNum(p->token), SWZ_XYZW)
                                                               : D9TempSrc(D9RegNum(p->token), SWZ_XYZW);
            if (modifier == SM_DZ || modifier == SM_DW) {
                UINT d = temp(t);
                D9Op1(c, OP_MOV, D9Temp(d, 0xF), coords);
                D9Op2(c, OP_DIV, D9Temp(d, 0xF), D9TempSrc(d, SWZ_XYZW), D9TempSrc(d, REPL(modifier == SM_DZ ? 2 : 3)));
                coords = D9TempSrc(d, SWZ_XYZW);
            }
            sample_stage(t, m, coords, FALSE, scratch);
            write_dest(t, inst, scratch, dst_mask(inst->dst.token));
        } else {
            sample_stage(t, m, texcoord_input(t, m, SWZ_XYZW), t->key->projected >> m & 1, treg(m));
        }
        return TRUE;
    case D9_OP_TEXCOORD:
        if (v14) {
            const D9Param *p = &inst->src[0];
            UINT modifier = (p->token >> 24) & 0xF;
            D9Op1(c, OP_MOV, D9Temp(scratch, 0xF), texcoord_input(t, D9RegNum(p->token), SWZ_XYZW));
            if (modifier == SM_DZ || modifier == SM_DW)
                D9Op2(c, OP_DIV, D9Temp(scratch, 0xF), D9TempSrc(scratch, SWZ_XYZW),
                      D9TempSrc(scratch, REPL(modifier == SM_DZ ? 2 : 3)));
            D9Op1(c, OP_MOV, D9Temp(scratch, 8), D9ImmF(1.0f));
            write_dest(t, inst, scratch, dst_mask(inst->dst.token));
        } else {
            /* the coordinates clamped to [0, 1], w 1. */
            D9Op1(c, OP_MOV | 1u << 13, D9Temp(treg(m), 7), texcoord_input(t, m, SWZ_XYZW));
            D9Op1(c, OP_MOV, D9Temp(treg(m), 8), D9ImmF(1.0f));
        }
        return TRUE;
    case D9_OP_TEXKILL: {
        /* of the coordinates' x, y and z in 1.0 to 1.3, of the register in 1.4. */
        D9Opnd v = v14 && D9RegType(inst->dst.token) == RT_TEMP ? D9TempSrc(m, SWZ_XYZW) : texcoord_input(t, m, SWZ_XYZW);
        D9Op2(c, OP_LT, D9Temp(scratch, 7), v, D9ImmF(0.0f));
        D9Op2(c, OP_OR, D9Temp(scratch, 1), D9TempSrc(scratch, REPL(0)), D9TempSrc(scratch, REPL(1)));
        D9Op2(c, OP_OR, D9Temp(scratch, 1), D9TempSrc(scratch, REPL(0)), D9TempSrc(scratch, REPL(2)));
        D9Opnd cond = D9TempScalar(scratch, 0);
        D9Emit(c, OP_DISCARD | 1u << 18, &cond, 1);
        return TRUE;
    }
    case D9_OP_TEXBEM:
    case D9_OP_TEXBEML: {
        /* the stage's coordinates moved by its bump matrix times the source's red and green. */
        UINT n = D9RegNum(inst->src[0].token), d = temp(t);
        D9Op1(c, OP_MOV, D9Temp(d, 0xF), texcoord_input(t, m, SWZ_XYZW));
        D9Op3(c, OP_MAD, D9Temp(d, 3), D9TempSrc(treg(n), REPL(0)), bump(m, SWZ(0, 1, 0, 1)), D9TempSrc(d, SWZ_XYZW));
        D9Op3(c, OP_MAD, D9Temp(d, 3), D9TempSrc(treg(n), REPL(1)), bump(m, SWZ(2, 3, 2, 3)), D9TempSrc(d, SWZ_XYZW));
        sample_stage(t, m, D9TempSrc(d, SWZ_XYZW), FALSE, treg(m));
        if (op == D9_OP_TEXBEML) {
            D9Op3(c, OP_MAD | 1u << 13, D9Temp(d, 1), D9TempSrc(treg(n), REPL(2)), D9Cb(2, 12 + m, REPL(0), -1, 0),
                  D9Cb(2, 12 + m, REPL(1), -1, 0));
            D9Op2(c, OP_MUL, D9Temp(treg(m), 7), D9TempSrc(treg(m), SWZ_XYZW), D9TempSrc(d, REPL(0)));
        }
        return TRUE;
    }
    case D9_OP_TEXREG2AR:
    case D9_OP_TEXREG2GB:
    case D9_OP_TEXREG2RGB: {
        UINT n = D9RegNum(inst->src[0].token);
        UINT swizzle = op == D9_OP_TEXREG2AR ? SWZ(3, 0, 0, 0) : op == D9_OP_TEXREG2GB ? SWZ(1, 2, 2, 2) : SWZ(0, 1, 2, 2);
        sample_stage(t, m, D9TempSrc(treg(n), swizzle), FALSE, treg(m));
        return TRUE;
    }
    case D9_OP_TEXM3x2PAD:
    case D9_OP_TEXM3x3PAD: {
        /* a row: the dot product of the stage's coordinates and the source, kept for the next. */
        UINT n = D9RegNum(inst->src[0].token);
        UINT row = t->matrix_row++;
        D9Op2(c, OP_DP3, D9Temp(t->r_matrix, 1u << row), texcoord_input(t, m, SWZ_XYZW), D9TempSrc(treg(n), SWZ_XYZW));
        return TRUE;
    }
    case D9_OP_TEXM3x2TEX:
    case D9_OP_TEXM3x2DEPTH:
    case D9_OP_TEXM3x3TEX:
    case D9_OP_TEXM3x3:
    case D9_OP_TEXM3x3SPEC:
    case D9_OP_TEXM3x3VSPEC: {
        UINT n = D9RegNum(inst->src[0].token);
        UINT row = t->matrix_row;
        D9Op2(c, OP_DP3, D9Temp(t->r_matrix, 1u << row), texcoord_input(t, m, SWZ_XYZW), D9TempSrc(treg(n), SWZ_XYZW));
        t->matrix_row = 0;
        D9Opnd v = D9TempSrc(t->r_matrix, SWZ_XYZW);
        if (op == D9_OP_TEXM3x2TEX) {
            D9Op1(c, OP_MOV, D9Temp(t->r_matrix, 0xC), D9ImmF(0.0f));
            sample_stage(t, m, v, FALSE, treg(m));
        } else if (op == D9_OP_TEXM3x2DEPTH) {
            /* the depth: z over w, the two rows, 1 where w is 0. */
            D9Op2(c, OP_DIV, D9Temp(t->r_out[4], 1), D9TempSrc(t->r_matrix, REPL(0)), D9TempSrc(t->r_matrix, REPL(1)));
            D9Op2(c, OP_EQ, D9Temp(scratch, 1), D9TempSrc(t->r_matrix, REPL(1)), D9ImmF(0.0f));
            D9Op3(c, OP_MOVC, D9Temp(t->r_out[4], 1), D9TempSrc(scratch, REPL(0)), D9ImmF(1.0f),
                  D9TempSrc(t->r_out[4], REPL(0)));
        } else if (op == D9_OP_TEXM3x3) {
            D9Op1(c, OP_MOV, D9Temp(treg(m), 7), v);
            D9Op1(c, OP_MOV, D9Temp(treg(m), 8), D9ImmF(1.0f));
        } else if (op == D9_OP_TEXM3x3TEX) {
            sample_stage(t, m, v, FALSE, treg(m));
        } else {
            /* the eye's reflection about the normal the rows made: 2 N (N.E) / (N.N) - E. */
            UINT e = temp(t);
            if (op == D9_OP_TEXM3x3SPEC) {
                D9Op1(c, OP_MOV, D9Temp(e, 0xF), source(t, &inst->src[1]));
            } else {
                D9Op1(c, OP_MOV, D9Temp(e, 1), texcoord_input(t, m - 2, REPL(3)));
                D9Op1(c, OP_MOV, D9Temp(e, 2), texcoord_input(t, m - 1, REPL(3)));
                D9Op1(c, OP_MOV, D9Temp(e, 4), texcoord_input(t, m, REPL(3)));
            }
            D9Op2(c, OP_DP3, D9Temp(scratch, 1), v, D9TempSrc(e, SWZ_XYZW));
            D9Op2(c, OP_DP3, D9Temp(scratch, 2), v, v);
            D9Op2(c, OP_DIV, D9Temp(scratch, 1), D9TempSrc(scratch, REPL(0)), D9TempSrc(scratch, REPL(1)));
            D9Op2(c, OP_ADD, D9Temp(scratch, 1), D9TempSrc(scratch, REPL(0)), D9TempSrc(scratch, REPL(0)));
            D9Op3(c, OP_MAD, D9Temp(e, 7), v, D9TempSrc(scratch, REPL(0)), D9Neg(D9TempSrc(e, SWZ_XYZW)));
            sample_stage(t, m, D9TempSrc(e, SWZ_XYZW), FALSE, treg(m));
        }
        return TRUE;
    }
    case D9_OP_TEXDP3:
    case D9_OP_TEXDP3TEX: {
        UINT n = D9RegNum(inst->src[0].token);
        D9Op2(c, OP_DP3, D9Temp(scratch, 0xF), texcoord_input(t, m, SWZ_XYZW), D9TempSrc(treg(n), SWZ_XYZW));
        if (op == D9_OP_TEXDP3) {
            D9Op1(c, OP_MOV, D9Temp(treg(m), 0xF), D9TempSrc(scratch, REPL(0)));
        } else {
            D9Op1(c, OP_MOV, D9Temp(scratch, 0xE), D9ImmF(0.0f));
            sample_stage(t, m, D9TempSrc(scratch, SWZ_XYZW), FALSE, treg(m));
        }
        return TRUE;
    }
    case D9_OP_TEXDEPTH:
        /* 1.4's r5: red over green, 1 where green is 0. */
        D9Op2(c, OP_DIV, D9Temp(t->r_out[4], 1), D9TempSrc(5, REPL(0)), D9TempSrc(5, REPL(1)));
        D9Op2(c, OP_EQ, D9Temp(scratch, 1), D9TempSrc(5, REPL(1)), D9ImmF(0.0f));
        D9Op3(c, OP_MOVC, D9Temp(t->r_out[4], 1), D9TempSrc(scratch, REPL(0)), D9ImmF(1.0f),
              D9TempSrc(t->r_out[4], REPL(0)));
        return TRUE;
    case D9_OP_BEM: {
        /* dst.rg = src0.rg + the destination stage's bump matrix times src1.rg. */
        D9Opnd a = source(t, &inst->src[0]), b = source(t, &inst->src[1]);
        D9Opnd bx = b, by = b;
        bx.words[0] = (bx.words[0] & ~(0xFFu << 4)) | REPL(0) << 4;
        by.words[0] = (by.words[0] & ~(0xFFu << 4)) | REPL(1) << 4;
        D9Op1(c, OP_MOV, D9Temp(scratch, 0xF), a);
        D9Op3(c, OP_MAD, D9Temp(scratch, 3), bx, bump(m, SWZ(0, 1, 0, 1)), D9TempSrc(scratch, SWZ_XYZW));
        D9Op3(c, OP_MAD, D9Temp(scratch, 3), by, bump(m, SWZ(2, 3, 2, 3)), D9TempSrc(scratch, SWZ_XYZW));
        write_dest(t, inst, scratch, 3);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

static void D9TranslateEpilogue(Tx *t);

static void translate(Tx *t, const D9Inst *inst)
{
    const D9ShaderInfo *info = t->info;
    UINT op = inst->opcode;
    UINT mask = inst->has_dst ? dst_mask(inst->dst.token) : 0xF;
    UINT save_next = t->next_temp;
    UINT scratch = 0;
    if (info->pixel && info->major < 2) {
        BOOL handled = ps1_texture(t, inst);
        t->next_temp = save_next;
        if (handled)
            return;
    }
    switch (op) {
    case D9_OP_NOP:
    case D9_OP_DCL:
    case D9_OP_DEF:
    case D9_OP_DEFI:
    case D9_OP_DEFB:
    case D9_OP_COMMENT:
    case D9_OP_PHASE:
        return;
    case D9_OP_MOV:
    case D9_OP_ADD:
    case D9_OP_MAD:
    case D9_OP_MUL:
    case D9_OP_DP3:
    case D9_OP_DP4:
    case D9_OP_MIN:
    case D9_OP_MAX:
    case D9_OP_FRC:
    case D9_OP_DSX:
    case D9_OP_DSY: {
        if (op == D9_OP_MOV && D9RegType(inst->dst.token) == RT_ADDR && !info->pixel) {
            /* vs 1.1's mov to a0 rounds down. */
            scratch = temp(t);
            D9Op1(&t->code, OP_ROUND_NI, D9Temp(scratch, mask), source(t, &inst->src[0]));
            D9Op1(&t->code, OP_FTOI, D9Temp(t->r_addr, mask), D9TempSrc(scratch, SWZ_XYZW));
            break;
        }
        scratch = temp(t);
        D9Opnd ops[4] = {D9Temp(scratch, mask)};
        for (UINT i = 0; i < inst->src_count; i++)
            ops[1 + i] = source(t, &inst->src[i]);
        D9Emit(&t->code, simple_opcode(op), ops, 1 + inst->src_count);
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_SUB:
        scratch = temp(t);
        D9Op2(&t->code, OP_ADD, D9Temp(scratch, mask), source(t, &inst->src[0]), D9Neg(source(t, &inst->src[1])));
        write_dest(t, inst, scratch, mask);
        break;
    case D9_OP_ABS:
        scratch = temp(t);
        D9Op1(&t->code, OP_MOV, D9Temp(scratch, mask), D9Abs(source(t, &inst->src[0])));
        write_dest(t, inst, scratch, mask);
        break;
    case D9_OP_RCP:
        scratch = temp(t);
        D9Op1(&t->code, OP_RCP, D9Temp(scratch, mask), source(t, &inst->src[0]));
        write_dest(t, inst, scratch, mask);
        break;
    case D9_OP_RSQ:
        scratch = temp(t);
        D9Op1(&t->code, OP_RSQ, D9Temp(scratch, mask), D9Abs(source(t, &inst->src[0])));
        write_dest(t, inst, scratch, mask);
        break;
    case D9_OP_EXP:
    case D9_OP_EXPP:
        scratch = temp(t);
        if (op == D9_OP_EXPP && info->major < 2) {
            /* vs 1.1's expp: 2 to the floor, the fraction, 2 to the value, 1. */
            D9Opnd s = source(t, &inst->src[0]);
            UINT f = temp(t);
            D9Op1(&t->code, OP_ROUND_NI, D9Temp(f, 1), s);
            D9Op1(&t->code, OP_EXP, D9Temp(scratch, 1), D9TempSrc(f, REPL(0)));
            D9Op2(&t->code, OP_ADD, D9Temp(scratch, 2), s, D9Neg(D9TempSrc(f, REPL(0))));
            D9Op1(&t->code, OP_EXP, D9Temp(scratch, 4), s);
            D9Op1(&t->code, OP_MOV, D9Temp(scratch, 8), D9ImmF(1.0f));
        } else {
            D9Op1(&t->code, OP_EXP, D9Temp(scratch, mask), source(t, &inst->src[0]));
        }
        write_dest(t, inst, scratch, mask);
        break;
    case D9_OP_LOG:
    case D9_OP_LOGP:
        scratch = temp(t);
        if (op == D9_OP_LOGP && info->major < 2) {
            /* vs 1.1's logp: the exponent, the mantissa, log2 of the value, 1. */
            D9Opnd s = D9Abs(source(t, &inst->src[0]));
            UINT l = temp(t);
            D9Op1(&t->code, OP_LOG, D9Temp(l, 1), s);
            D9Op1(&t->code, OP_ROUND_NI, D9Temp(scratch, 1), D9TempSrc(l, REPL(0)));
            D9Op1(&t->code, OP_EXP, D9Temp(l, 2), D9Neg(D9TempSrc(scratch, REPL(0))));
            D9Op2(&t->code, OP_MUL, D9Temp(scratch, 2), s, D9TempSrc(l, REPL(1)));
            D9Op1(&t->code, OP_MOV, D9Temp(scratch, 4), D9TempSrc(l, REPL(0)));
            D9Op1(&t->code, OP_MOV, D9Temp(scratch, 8), D9ImmF(1.0f));
        } else {
            D9Op1(&t->code, OP_LOG, D9Temp(scratch, mask), D9Abs(source(t, &inst->src[0])));
        }
        write_dest(t, inst, scratch, mask);
        break;
    case D9_OP_POW: {
        scratch = temp(t);
        D9Op1(&t->code, OP_LOG, D9Temp(scratch, mask), D9Abs(source(t, &inst->src[0])));
        D9Op2(&t->code, OP_MUL, D9Temp(scratch, mask), D9TempSrc(scratch, SWZ_XYZW), source(t, &inst->src[1]));
        D9Op1(&t->code, OP_EXP, D9Temp(scratch, mask), D9TempSrc(scratch, SWZ_XYZW));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_SLT:
    case D9_OP_SGE:
        scratch = temp(t);
        D9Op2(&t->code, op == D9_OP_SLT ? OP_LT : OP_GE, D9Temp(scratch, mask), source(t, &inst->src[0]),
              source(t, &inst->src[1]));
        D9Op2(&t->code, OP_AND, D9Temp(scratch, mask), D9TempSrc(scratch, SWZ_XYZW), D9Imm4(fbits(1), fbits(1), fbits(1), fbits(1)));
        write_dest(t, inst, scratch, mask);
        break;
    case D9_OP_SGN: {
        scratch = temp(t);
        UINT n = temp(t);
        D9Opnd s = source(t, &inst->src[0]);
        D9Op2(&t->code, OP_LT, D9Temp(scratch, mask), D9ImmF(0.0f), s);
        D9Op2(&t->code, OP_LT, D9Temp(n, mask), s, D9ImmF(0.0f));
        D9Op2(&t->code, OP_IADD, D9Temp(scratch, mask), D9TempSrc(n, SWZ_XYZW), D9Neg(D9TempSrc(scratch, SWZ_XYZW)));
        D9Op1(&t->code, OP_ITOF, D9Temp(scratch, mask), D9TempSrc(scratch, SWZ_XYZW));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_LRP: {
        scratch = temp(t);
        D9Opnd c = source(t, &inst->src[2]);
        D9Op2(&t->code, OP_ADD, D9Temp(scratch, mask), source(t, &inst->src[1]), D9Neg(c));
        D9Op3(&t->code, OP_MAD, D9Temp(scratch, mask), source(t, &inst->src[0]), D9TempSrc(scratch, SWZ_XYZW), c);
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_CMP: {
        scratch = temp(t);
        D9Op2(&t->code, OP_GE, D9Temp(scratch, mask), source(t, &inst->src[0]), D9ImmF(0.0f));
        D9Op3(&t->code, OP_MOVC, D9Temp(scratch, mask), D9TempSrc(scratch, SWZ_XYZW), source(t, &inst->src[1]),
              source(t, &inst->src[2]));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_CND: {
        /* ps 1.1 to 1.3 test r0.a alone, as their only form; 1.4 each component. */
        scratch = temp(t);
        D9Op2(&t->code, OP_LT, D9Temp(scratch, mask), D9ImmF(0.5f), source(t, &inst->src[0]));
        D9Op3(&t->code, OP_MOVC, D9Temp(scratch, mask), D9TempSrc(scratch, SWZ_XYZW), source(t, &inst->src[1]),
              source(t, &inst->src[2]));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_DP2ADD: {
        scratch = temp(t);
        D9Op2(&t->code, OP_DP2, D9Temp(scratch, mask), source(t, &inst->src[0]), source(t, &inst->src[1]));
        D9Op2(&t->code, OP_ADD, D9Temp(scratch, mask), D9TempSrc(scratch, SWZ_XYZW), source(t, &inst->src[2]));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_CRS: {
        scratch = temp(t);
        D9Opnd a = source(t, &inst->src[0]), b = source(t, &inst->src[1]);
        UINT n = temp(t);
        D9Opnd a1 = a, b1 = b, a2 = a, b2 = b;
        /* a.yzx * b.zxy - a.zxy * b.yzx, the operands' own swizzles composed. */
        UINT sa = (a.words[0] >> 4) & 0xFF, sb = (b.words[0] >> 4) & 0xFF;
#define PICK(s, i) (((s) >> ((i)*2)) & 3)
#define COMPOSE(s, x, y, z) (PICK(s, x) | PICK(s, y) << 2 | PICK(s, z) << 4 | PICK(s, 3) << 6)
        if ((a.words[0] & 0xC) == (SELECT_SWIZZLE << 2)) {
            a1.words[0] = (a.words[0] & ~(0xFFu << 4)) | COMPOSE(sa, 1, 2, 0) << 4;
            a2.words[0] = (a.words[0] & ~(0xFFu << 4)) | COMPOSE(sa, 2, 0, 1) << 4;
        }
        if ((b.words[0] & 0xC) == (SELECT_SWIZZLE << 2)) {
            b1.words[0] = (b.words[0] & ~(0xFFu << 4)) | COMPOSE(sb, 2, 0, 1) << 4;
            b2.words[0] = (b.words[0] & ~(0xFFu << 4)) | COMPOSE(sb, 1, 2, 0) << 4;
        }
#undef COMPOSE
#undef PICK
        D9Op2(&t->code, OP_MUL, D9Temp(n, 7), a2, b2);
        D9Op3(&t->code, OP_MAD, D9Temp(scratch, 7), a1, b1, D9Neg(D9TempSrc(n, SWZ_XYZW)));
        write_dest(t, inst, scratch, mask & 7);
        break;
    }
    case D9_OP_NRM: {
        scratch = temp(t);
        D9Opnd s = source(t, &inst->src[0]);
        D9Op2(&t->code, OP_DP3, D9Temp(scratch, 8), s, s);
        D9Op1(&t->code, OP_RSQ, D9Temp(scratch, 8), D9TempSrc(scratch, REPL(3)));
        D9Op2(&t->code, OP_MUL, D9Temp(scratch, mask), s, D9TempSrc(scratch, REPL(3)));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_SINCOS: {
        scratch = temp(t);
        D9Opnd s = source(t, &inst->src[0]);
        D9Opnd ops[3] = {D9Temp(scratch, 2), D9Temp(scratch, 1), s};
        D9Emit(&t->code, OP_SINCOS, ops, 3);
        write_dest(t, inst, scratch, mask & 3);
        break;
    }
    case D9_OP_LIT: {
        scratch = temp(t);
        UINT c = temp(t);
        D9Opnd s = source(t, &inst->src[0]);
        D9Op1(&t->code, OP_MOV, D9Temp(scratch, 0xF), D9Imm4(fbits(1), 0, 0, fbits(1)));
        D9Op1(&t->code, OP_MOV, D9Temp(c, 0xF), s);
        D9Op2(&t->code, OP_MAX, D9Temp(scratch, 2), D9TempSrc(c, REPL(0)), D9ImmF(0.0f));
        /* the power clamped to [-128, 128], applied when x and y are positive. */
        D9Op2(&t->code, OP_MAX, D9Temp(c, 8), D9TempSrc(c, REPL(3)), D9ImmF(-127.9961f));
        D9Op2(&t->code, OP_MIN, D9Temp(c, 8), D9TempSrc(c, REPL(3)), D9ImmF(127.9961f));
        D9Op1(&t->code, OP_LOG, D9Temp(scratch, 4), D9TempSrc(c, REPL(1)));
        D9Op2(&t->code, OP_MUL, D9Temp(scratch, 4), D9TempSrc(scratch, REPL(2)), D9TempSrc(c, REPL(3)));
        D9Op1(&t->code, OP_EXP, D9Temp(scratch, 4), D9TempSrc(scratch, REPL(2)));
        D9Op2(&t->code, OP_LT, D9Temp(c, 3), D9ImmF(0.0f), D9TempSrc(c, SWZ_XYZW));
        D9Op2(&t->code, OP_AND, D9Temp(c, 1), D9TempSrc(c, REPL(0)), D9TempSrc(c, REPL(1)));
        D9Op2(&t->code, OP_AND, D9Temp(scratch, 4), D9TempSrc(scratch, REPL(2)), D9TempSrc(c, REPL(0)));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_DST: {
        scratch = temp(t);
        D9Opnd a = source(t, &inst->src[0]), b = source(t, &inst->src[1]);
        D9Op1(&t->code, OP_MOV, D9Temp(scratch, 1), D9ImmF(1.0f));
        D9Op2(&t->code, OP_MUL, D9Temp(scratch, 2), a, b);
        D9Op1(&t->code, OP_MOV, D9Temp(scratch, 4), a);
        D9Op1(&t->code, OP_MOV, D9Temp(scratch, 8), b);
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_M4x4:
    case D9_OP_M4x3:
    case D9_OP_M3x4:
    case D9_OP_M3x3:
    case D9_OP_M3x2: {
        static const BYTE ROWS[] = {4, 3, 4, 3, 2}, COLUMNS[] = {4, 4, 3, 3, 3};
        UINT k = op - D9_OP_M4x4;
        scratch = temp(t);
        matrix(t, inst, scratch, ROWS[k], COLUMNS[k]);
        write_dest(t, inst, scratch, mask & ((1u << ROWS[k]) - 1));
        break;
    }
    case D9_OP_MOVA: {
        scratch = temp(t);
        D9Op1(&t->code, OP_ROUND_NE, D9Temp(scratch, mask), source(t, &inst->src[0]));
        D9Op1(&t->code, OP_FTOI, D9Temp(t->r_addr, mask), D9TempSrc(scratch, SWZ_XYZW));
        break;
    }
    case D9_OP_SETP: {
        scratch = temp(t);
        compare(t, D9Temp(scratch, mask), inst->control & 7, source(t, &inst->src[0]), source(t, &inst->src[1]));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_TEX:
    case D9_OP_TEXLDL:
    case D9_OP_TEXLDD: {
        scratch = temp(t);
        UINT sampler = D9RegNum(inst->src[1].token);
        sample(t, inst, scratch, &inst->src[0], sampler, src_swizzle(inst->src[1].token));
        write_dest(t, inst, scratch, mask);
        break;
    }
    case D9_OP_TEXKILL: {
        scratch = temp(t);
        UINT reg = D9RegNum(inst->dst.token);
        D9Param p = {(inst->dst.token & ~(0xFFu << 16)) | (SWZ_XYZW << 16)};
        (void)reg;
        D9Op2(&t->code, OP_LT, D9Temp(scratch, 0xF), source(t, &p), D9ImmF(0.0f));
        UINT m = dst_mask(inst->dst.token);
        UINT first = first_component(m);
        for (UINT i = first + 1; i < 4; i++)
            if (m & (1u << i))
                D9Op2(&t->code, OP_OR, D9Temp(scratch, 1u << first), D9TempSrc(scratch, REPL(first)),
                      D9TempSrc(scratch, REPL(i)));
        D9Opnd cond = D9TempScalar(scratch, first);
        D9Emit(&t->code, OP_DISCARD | 1u << 18, &cond, 1);
        break;
    }
    case D9_OP_IF: {
        D9Opnd cond = source(t, &inst->src[0]);
        cond.words[0] = (cond.words[0] & ~0xFFFu) | 2 | SELECT_SELECT1 << 2 | (src_swizzle(inst->src[0].token) & 3) << 4;
        if ((inst->src[0].token >> 24 & 0xF) == SM_NOT) {
            D9Emit(&t->code, OP_IF, &cond, 1);
        } else {
            D9Emit(&t->code, OP_IF | 1u << 18, &cond, 1);
        }
        return;
    }
    case D9_OP_IFC:
    case D9_OP_BREAKC: {
        UINT c = temp(t);
        compare(t, D9Temp(c, 1), inst->control & 7, source(t, &inst->src[0]), source(t, &inst->src[1]));
        D9Opnd cond = D9TempScalar(c, 0);
        D9Emit(&t->code, (op == D9_OP_IFC ? OP_IF : OP_BREAKC) | 1u << 18, &cond, 1);
        t->next_temp = save_next;
        return;
    }
    case D9_OP_BREAKP: {
        D9Opnd cond = D9TempScalar(t->r_pred, src_swizzle(inst->src[0].token) & 3);
        BOOL negate = ((inst->src[0].token >> 24) & 0xF) == SM_NOT;
        D9Emit(&t->code, OP_BREAKC | (negate ? 0 : 1u << 18), &cond, 1);
        return;
    }
    case D9_OP_ELSE:
        D9Emit(&t->code, OP_ELSE, nullptr, 0);
        return;
    case D9_OP_ENDIF:
        D9Emit(&t->code, OP_ENDIF, nullptr, 0);
        return;
    case D9_OP_BREAK:
        D9Emit(&t->code, OP_BREAK, nullptr, 0);
        return;
    case D9_OP_LOOP:
    case D9_OP_REP: {
        /* aL in x, the iterations left in y, from the integer constant's count, start and step. */
        UINT l = temp(t);
        save_next = t->next_temp;
        const D9Param *ip = &inst->src[op == D9_OP_LOOP ? 1 : 0];
        D9Opnd ic = D9Cb(1, D9RegNum(ip->token), SWZ_XYZW, -1, 0);
        D9Opnd count = ic, start = ic;
        count.words[0] = (count.words[0] & ~(0xFFu << 4)) | REPL(0) << 4;
        start.words[0] = (start.words[0] & ~(0xFFu << 4)) | REPL(1) << 4;
        D9Op1(&t->code, OP_MOV, D9Temp(l, 2), count);
        if (op == D9_OP_LOOP)
            D9Op1(&t->code, OP_MOV, D9Temp(l, 1), start);
        D9Emit(&t->code, OP_LOOP, nullptr, 0);
        D9Opnd left = D9TempScalar(l, 1);
        D9Emit(&t->code, OP_BREAKC, &left, 1);
        if (t->loop_depth < RTL_NUMBER_OF(t->loop_temps)) {
            t->loop_temps[t->loop_depth] = l;
            t->loop_kinds[t->loop_depth] = op == D9_OP_LOOP;
            t->loop_steps[t->loop_depth] = D9RegNum(ip->token);
            t->loop_depth++;
        } else {
            fail(t, "loops nested too deep");
        }
        t->next_temp = save_next;
        return;
    }
    case D9_OP_ENDLOOP:
    case D9_OP_ENDREP: {
        if (!t->loop_depth) {
            fail(t, "an end of a loop never begun");
            return;
        }
        t->loop_depth--;
        UINT l = t->loop_temps[t->loop_depth];
        if (t->loop_kinds[t->loop_depth]) {
            D9Opnd step = D9Cb(1, t->loop_steps[t->loop_depth], REPL(2), -1, 0);
            D9Op2(&t->code, OP_IADD, D9Temp(l, 1), D9TempSrc(l, REPL(0)), step);
        }
        D9Op2(&t->code, OP_IADD, D9Temp(l, 2), D9TempSrc(l, REPL(1)), D9Imm4(~0u, ~0u, ~0u, ~0u));
        D9Emit(&t->code, OP_ENDLOOP, nullptr, 0);
        return;
    }
    case D9_OP_CALL: {
        D9Opnd label = D9Src(OPERAND_LABEL, D9RegNum(inst->src[0].token), 0);
        label.words[0] = OPERAND_LABEL << 12 | 1u << 20;
        D9Emit(&t->code, OP_CALL, &label, 1);
        return;
    }
    case D9_OP_CALLNZ: {
        D9Opnd ops[2];
        ops[0] = source(t, &inst->src[1]);
        ops[0].words[0] = (ops[0].words[0] & ~0xFFFu) | 2 | SELECT_SELECT1 << 2 | (src_swizzle(inst->src[1].token) & 3) << 4;
        ops[1] = D9Src(OPERAND_LABEL, D9RegNum(inst->src[0].token), 0);
        ops[1].words[0] = OPERAND_LABEL << 12 | 1u << 20;
        BOOL negate = ((inst->src[1].token >> 24) & 0xF) == SM_NOT;
        D9Emit(&t->code, OP_CALLC | (negate ? 0 : 1u << 18), ops, 2);
        return;
    }
    case D9_OP_LABEL: {
        if (t->in_main) {
            D9TranslateEpilogue(t);
            t->in_main = FALSE;
        }
        D9Opnd label = D9Src(OPERAND_LABEL, D9RegNum(inst->src[0].token), 0);
        label.words[0] = OPERAND_LABEL << 12 | 1u << 20;
        D9Emit(&t->code, OP_LABEL, &label, 1);
        return;
    }
    case D9_OP_RET:
        if (t->in_main)
            D9TranslateEpilogue(t);
        else
            D9Emit(&t->code, OP_RET, nullptr, 0);
        return;
    default:
        fail(t, "an instruction of an unknown kind");
        D9Log("d3d9: no translation of opcode %u of shader model %u.%u", op, info->major, info->minor);
        return;
    }
    t->next_temp = save_next;
}

/* ------------------------------------------------------------------ the end of the program */

/* the vertex shader's position, from its output of either model. */
static int position_temp(Tx *t)
{
    const D9ShaderInfo *info = t->info;
    if (info->major < 3)
        return info->written & D9_OUT_POS ? (int)t->r_out[0] : -1;
    for (UINT r = 0; r < 12; r++)
        if ((info->out_declared & (1u << r)) && info->out_usage[r] == D3DDECLUSAGE_POSITION && !info->out_index[r])
            return (int)t->r_out[r];
    return -1;
}

static void vs_epilogue(Tx *t)
{
    const D9ShaderInfo *info = t->info;
    const D9ShaderKey *key = t->key;
    D9Code *c = &t->code;
    int pos = position_temp(t);
    UINT p = temp(t);
    if (pos >= 0)
        D9Op1(c, OP_MOV, D9Temp(p, 0xF), D9TempSrc(pos, SWZ_XYZW));
    else
        D9Op1(c, OP_MOV, D9Temp(p, 0xF), D9Imm4(0, 0, 0, fbits(1)));
    /* user clip planes in clip space, those not enabled always inside. */
    if (key->clip_planes)
        for (UINT i = 0; i < 6; i++) {
            D9Opnd dst = D9Dst(OPERAND_OUTPUT, i < 4 ? D9_SLOT_CLIP : D9_SLOT_CLIP + 1, 1u << (i & 3));
            if (key->clip_planes & (1u << i))
                D9Op2(c, OP_DP4, dst, D9TempSrc(p, SWZ_XYZW), D9Cb(2, 1 + i, SWZ_XYZW, -1, 0));
            else
                D9Op1(c, OP_MOV, dst, D9ImmF(1.0f));
        }
    /* direct3d 9's pixel centers are at integers: the position moves half a pixel right and down. */
    D9Op3(c, OP_MAD, D9Dst(OPERAND_OUTPUT, D9_SLOT_POSITION, 3), D9TempSrc(p, REPL(3)),
          D9Cb(2, 0, SWZ(0, 1, 0, 0), -1, 0), D9TempSrc(p, SWZ_XYZW));
    D9Op1(c, OP_MOV, D9Dst(OPERAND_OUTPUT, D9_SLOT_POSITION, 0xC), D9TempSrc(p, SWZ_XYZW));
    BOOL size_written = FALSE;
    if (info->major < 3) {
        if (info->written & D9_OUT_FOG)
            D9Op1(c, OP_MOV, D9Dst(OPERAND_OUTPUT, 10, 1), D9TempSrc(t->r_out[1], REPL(0)));
        if (info->written & D9_OUT_PTS) {
            D9Op2(c, OP_MAX, D9Temp(p, 1), D9TempSrc(t->r_out[2], REPL(0)), D9Cb(2, 7, REPL(0), -1, 0));
            D9Op2(c, OP_MIN, D9Dst(OPERAND_OUTPUT, D9_SLOT_POINT_SIZE, 1), D9TempSrc(p, REPL(0)),
                  D9Cb(2, 7, REPL(1), -1, 0));
            size_written = TRUE;
        }
        for (UINT i = 0; i < 2; i++)
            if (info->written & (D9_OUT_D0 << i))
                D9Op1(c, OP_MOV | 1u << 13, D9Dst(OPERAND_OUTPUT, 8 + i, 0xF), D9TempSrc(t->r_out[3 + i], SWZ_XYZW));
        for (UINT i = 0; i < 8; i++)
            if (info->written & (D9_OUT_T0 << i))
                D9Op1(c, OP_MOV, D9Dst(OPERAND_OUTPUT, i, 0xF), D9TempSrc(t->r_out[5 + i], SWZ_XYZW));
    } else {
        for (UINT r = 0; r < 12; r++) {
            if (!(info->out_declared & (1u << r)))
                continue;
            UINT usage = info->out_usage[r], index = info->out_index[r];
            if (usage == D3DDECLUSAGE_POSITION && !index)
                continue;
            if (usage == D3DDECLUSAGE_PSIZE && !index) {
                D9Op2(c, OP_MAX, D9Temp(p, 1), D9TempSrc(t->r_out[r], REPL(0)), D9Cb(2, 7, REPL(0), -1, 0));
                D9Op2(c, OP_MIN, D9Dst(OPERAND_OUTPUT, D9_SLOT_POINT_SIZE, 1), D9TempSrc(p, REPL(0)),
                      D9Cb(2, 7, REPL(1), -1, 0));
                size_written = TRUE;
                continue;
            }
            int slot = D9VaryingSlot(usage, index);
            if (slot < 0) {
                fail(t, "more kinds of interpolated values than registers");
                continue;
            }
            D9Op1(c, OP_MOV, D9Dst(OPERAND_OUTPUT, slot, 0xF), D9TempSrc(t->r_out[r], SWZ_XYZW));
        }
    }
    if (!size_written && key->point_size)
        D9Op1(c, OP_MOV, D9Dst(OPERAND_OUTPUT, D9_SLOT_POINT_SIZE, 1), D9Cb(2, 0, REPL(2), -1, 0));
    t->writes_size = size_written || key->point_size;
}

static void ps_epilogue(Tx *t)
{
    const D9ShaderInfo *info = t->info;
    const D9ShaderKey *key = t->key;
    D9Code *c = &t->code;
    UINT color = info->major < 2 ? 0 : t->r_out[0];
    if (key->fog && info->major < 3) {
        UINT f = temp(t), d = temp(t);
        if (key->fog == 1) {
            if (key->ps_inputs & (1u << 10))
                D9Op1(c, OP_MOV | 1u << 13, D9Temp(f, 1), D9Src(OPERAND_INPUT, 10, REPL(0)));
            else
                D9Op1(c, OP_MOV, D9Temp(f, 1), D9ImmF(1.0f));
        } else {
            t->reads_position = TRUE;
            D9Opnd depth = D9Src(OPERAND_INPUT, D9_SLOT_POSITION, REPL(2));
            D9Opnd fog = D9Cb(2, 2, SWZ_XYZW, -1, 0);
            D9Opnd end = fog, density = fog, scale = fog;
            end.words[0] = (end.words[0] & ~(0xFFu << 4)) | REPL(1) << 4;
            density.words[0] = (density.words[0] & ~(0xFFu << 4)) | REPL(2) << 4;
            scale.words[0] = (scale.words[0] & ~(0xFFu << 4)) | REPL(3) << 4;
            if (key->fog == 4) {
                D9Op2(c, OP_ADD, D9Temp(d, 1), end, D9Neg(depth));
                D9Op2(c, OP_MUL | 1u << 13, D9Temp(f, 1), D9TempSrc(d, REPL(0)), scale);
            } else {
                D9Op2(c, OP_MUL, D9Temp(d, 1), depth, density);
                if (key->fog == 3)
                    D9Op2(c, OP_MUL, D9Temp(d, 1), D9TempSrc(d, REPL(0)), D9TempSrc(d, REPL(0)));
                D9Op2(c, OP_MUL, D9Temp(d, 1), D9TempSrc(d, REPL(0)), D9ImmF(-1.442695f));
                D9Op1(c, OP_EXP | 1u << 13, D9Temp(f, 1), D9TempSrc(d, REPL(0)));
            }
        }
        /* the color toward the fog's: fog + f * (color - fog). */
        D9Opnd fog_color = D9Cb(2, 1, SWZ_XYZW, -1, 0);
        D9Op2(c, OP_ADD, D9Temp(d, 7), D9TempSrc(color, SWZ_XYZW), D9Neg(fog_color));
        D9Op3(c, OP_MAD, D9Temp(color, 7), D9TempSrc(f, REPL(0)), D9TempSrc(d, SWZ_XYZW), fog_color);
    }
    if (key->alpha_func && key->alpha_func != D3DCMP_ALWAYS) {
        UINT a = temp(t);
        if (key->alpha_func == D3DCMP_NEVER) {
            D9Opnd always = D9Imm(~0u);
            D9Emit(c, OP_DISCARD | 1u << 18, &always, 1);
        } else {
            /* alpha as the 8 bits the reference has. */
            D9Op2(c, OP_MUL, D9Temp(a, 1), D9TempSrc(color, REPL(3)), D9ImmF(255.0f));
            D9Op1(c, OP_ROUND_NE, D9Temp(a, 1), D9TempSrc(a, REPL(0)));
            static const BYTE KIND[] = {0, 4, 2, 6, 1, 5, 3, 0};
            compare(t, D9Temp(a, 2), KIND[key->alpha_func - 1], D9TempSrc(a, REPL(0)), D9Cb(2, 0, REPL(0), -1, 0));
            D9Opnd pass = D9TempScalar(a, 1);
            D9Emit(c, OP_DISCARD, &pass, 1);
        }
    }
    if (info->major < 2) {
        D9Op1(c, OP_MOV, D9Dst(OPERAND_OUTPUT, 0, 0xF), D9TempSrc(0, SWZ_XYZW));
    } else {
        for (UINT i = 0; i < 4; i++)
            if (info->written & (1u << i))
                D9Op1(c, OP_MOV, D9Dst(OPERAND_OUTPUT, i, 0xF), D9TempSrc(t->r_out[i], SWZ_XYZW));
    }
    if (info->writes_depth) {
        D9Opnd depth = {{1 | OPERAND_OUTPUT_DEPTH << 12}, 1};
        D9Op1(c, OP_MOV, depth, D9TempSrc(t->r_out[4], REPL(0)));
    }
}

static void D9TranslateEpilogue(Tx *t)
{
    UINT save = t->next_temp;
    if (t->info->pixel)
        ps_epilogue(t);
    else
        vs_epilogue(t);
    D9Emit(&t->code, OP_RET, nullptr, 0);
    t->next_temp = save;
}

/* ------------------------------------------------------------------ declarations */

static void dcl(D9Code *c, DWORD opcode, D9Opnd o, const DWORD *extra, UINT n)
{
    UINT length = 1 + o.count + n;
    D9CodePut(c, opcode | length << 24);
    for (UINT i = 0; i < o.count; i++)
        D9CodePut(c, o.words[i]);
    for (UINT i = 0; i < n; i++)
        D9CodePut(c, extra[i]);
}

static void add_element(D9Signature *sig, const char *name, UINT index, UINT sv, UINT type, UINT reg, UINT mask)
{
    if (sig->count < RTL_NUMBER_OF(sig->elements))
        sig->elements[sig->count++] = (D9SigElement){name, index, sv, type, reg, (BYTE)mask, (BYTE)mask};
}

static D9Opnd cb_decl(UINT slot, UINT size)
{
    D9Opnd o = {{2 | SELECT_SWIZZLE << 2 | SWZ_XYZW << 4 | OPERAND_CONSTANT_BUFFER << 12 | 2u << 20, slot, size}, 3};
    return o;
}

/* the semantic of an interpolated value's slot, for signatures. */
void D9SlotSemantic(UINT slot, const char **name, UINT *index)
{
    if (slot < 8) {
        *name = "TEXCOORD", *index = slot;
    } else if (slot < 10) {
        *name = "COLOR", *index = slot - 8;
    } else if (slot == 10) {
        *name = "FOG", *index = 0;
    } else {
        *name = "MIMEVARYING", *index = slot;
    }
}

static void declare(Tx *t, D9Code *d)
{
    const D9ShaderInfo *info = t->info;
    const D9ShaderKey *key = t->key;
    D9CodePut(d, (info->pixel ? 0u : 1u) << 16 | 5 << 4);
    D9CodePut(d, 0);
    UINT floats = D9FloatBucketSize(info);
    dcl(d, OP_DCL_CONSTANT_BUFFER | (info->relative ? 1u << 11 : 0), cb_decl(0, floats), nullptr, 0);
    dcl(d, OP_DCL_CONSTANT_BUFFER, cb_decl(1, 20), nullptr, 0);
    dcl(d, OP_DCL_CONSTANT_BUFFER, cb_decl(2, 24), nullptr, 0);
    for (UINT s = 0; s < 20; s++) {
        if (!(t->samplers_used & (1u << s)))
            continue;
        BOOL shadow = t->sampler_dims[s] & 0x80;
        dcl(d, OP_DCL_SAMPLER | (shadow ? 1u << 11 : 0), D9SamplerOpnd(s), nullptr, 0);
        UINT dim = (t->sampler_dims[s] & 0x7F) == 3 ? RES_TEXTURECUBE : (t->sampler_dims[s] & 0x7F) == 4 ? RES_TEXTURE3D : RES_TEXTURE2D;
        DWORD ret = RET_FLOAT | RET_FLOAT << 4 | RET_FLOAT << 8 | RET_FLOAT << 12;
        dcl(d, OP_DCL_RESOURCE | dim << 11, D9ResourceOpnd(s), &ret, 1);
    }
    if (!info->pixel) {
        for (UINT r = 0; r < 16; r++) {
            if (!(info->in_declared & (1u << r)))
                continue;
            UINT mask = info->in_mask[r] ? info->in_mask[r] : 0xF;
            UINT integer = key->int_inputs >> (r * 2) & 3;
            dcl(d, OP_DCL_INPUT, D9Dst(OPERAND_INPUT, r, mask), nullptr, 0);
            add_element(&t->inputs, D9UsageName(info->in_usage[r]), info->in_index[r], 0,
                        integer == 1 ? 1 : integer == 2 ? 2 : 3, r, mask);
        }
        DWORD sv = SV_POSITION;
        dcl(d, OP_DCL_OUTPUT_SIV, D9Dst(OPERAND_OUTPUT, D9_SLOT_POSITION, 0xF), &sv, 1);
        add_element(&t->outputs, "SV_Position", 0, SV_POSITION, 3, D9_SLOT_POSITION, 0xF);
        for (UINT slot = 0; slot < D9_SLOT_POSITION; slot++) {
            if (!(t->outputs_written & (1u << slot)))
                continue;
            const char *name;
            UINT index;
            D9SlotSemantic(slot, &name, &index);
            dcl(d, OP_DCL_OUTPUT, D9Dst(OPERAND_OUTPUT, slot, 0xF), nullptr, 0);
            add_element(&t->outputs, name, index, 0, 3, slot, 0xF);
        }
        if (t->writes_size) {
            sv = SV_MIME_POINT_SIZE;
            dcl(d, OP_DCL_OUTPUT_SIV, D9Dst(OPERAND_OUTPUT, D9_SLOT_POINT_SIZE, 1), &sv, 1);
            add_element(&t->outputs, "PSIZE", 0, SV_MIME_POINT_SIZE, 3, D9_SLOT_POINT_SIZE, 1);
        }
        if (key->clip_planes) {
            sv = SV_CLIP_DISTANCE;
            dcl(d, OP_DCL_OUTPUT_SIV, D9Dst(OPERAND_OUTPUT, D9_SLOT_CLIP, 0xF), &sv, 1);
            dcl(d, OP_DCL_OUTPUT_SIV, D9Dst(OPERAND_OUTPUT, D9_SLOT_CLIP + 1, 3), &sv, 1);
            add_element(&t->outputs, "SV_ClipDistance", 0, SV_CLIP_DISTANCE, 3, D9_SLOT_CLIP, 0xF);
            add_element(&t->outputs, "SV_ClipDistance", 1, SV_CLIP_DISTANCE, 3, D9_SLOT_CLIP + 1, 3);
        }
    } else {
        for (UINT slot = 0; slot < D9_SLOT_POSITION; slot++) {
            if (!(t->inputs_read & (1u << slot)))
                continue;
            const char *name;
            UINT index;
            D9SlotSemantic(slot, &name, &index);
            UINT interp = INTERP_LINEAR;
            if ((slot == 8 || slot == 9) && key->flat)
                interp = INTERP_CONSTANT;
            else if (t->centroid & (1u << slot))
                interp = INTERP_LINEAR_CENTROID;
            dcl(d, OP_DCL_INPUT_PS | interp << 11, D9Dst(OPERAND_INPUT, slot, 0xF), nullptr, 0);
            add_element(&t->inputs, name, index, 0, 3, slot, 0xF);
        }
        if (t->reads_position) {
            DWORD sv = SV_POSITION;
            dcl(d, OP_DCL_INPUT_PS_SIV | INTERP_LINEAR_NOPERSPECTIVE << 11, D9Dst(OPERAND_INPUT, D9_SLOT_POSITION, 0xF),
                &sv, 1);
            add_element(&t->inputs, "SV_Position", 0, SV_POSITION, 3, D9_SLOT_POSITION, 0xF);
        }
        if (info->vface) {
            DWORD sv = SV_IS_FRONT_FACE;
            dcl(d, OP_DCL_INPUT_PS_SGV | INTERP_CONSTANT << 11, D9Dst(OPERAND_INPUT, D9_SLOT_FACE, 1), &sv, 1);
            add_element(&t->inputs, "SV_IsFrontFace", 0, SV_IS_FRONT_FACE, 1, D9_SLOT_FACE, 1);
        }
        if (t->reads_point_coord) {
            DWORD sv = SV_MIME_POINT_COORD;
            dcl(d, OP_DCL_INPUT_PS_SIV | INTERP_LINEAR << 11, D9Dst(OPERAND_INPUT, D9_SLOT_POINT_COORD, 3), &sv, 1);
            add_element(&t->inputs, "MIMEPOINTCOORD", 0, SV_MIME_POINT_COORD, 3, D9_SLOT_POINT_COORD, 3);
        }
        UINT targets = info->major < 2 ? 1 : info->written & 0xF;
        for (UINT i = 0; i < 4; i++)
            if (targets & (1u << i)) {
                dcl(d, OP_DCL_OUTPUT, D9Dst(OPERAND_OUTPUT, i, 0xF), nullptr, 0);
                add_element(&t->outputs, "SV_Target", i, SV_TARGET, 3, i, 0xF);
            }
        if (info->writes_depth) {
            D9Opnd depth = {{1 | OPERAND_OUTPUT_DEPTH << 12}, 1};
            dcl(d, OP_DCL_OUTPUT, depth, nullptr, 0);
            add_element(&t->outputs, "SV_Depth", 0, SV_DEPTH, 3, ~0u, 1);
        }
    }
    D9CodePut(d, OP_DCL_TEMPS | 2u << 24);
    D9CodePut(d, t->temp_max);
}

/* ------------------------------------------------------------------ the program */

/* what the outputs and inputs of either stage are, before the code reads and writes them. */
static void plan_registers(Tx *t)
{
    const D9ShaderInfo *info = t->info;
    const D9ShaderKey *key = t->key;
    if (!info->pixel) {
        if (info->major < 3) {
            if (info->written & D9_OUT_FOG)
                t->outputs_written |= 1u << 10;
            for (UINT i = 0; i < 2; i++)
                if (info->written & (D9_OUT_D0 << i))
                    t->outputs_written |= 1u << (8 + i);
            for (UINT i = 0; i < 8; i++)
                if (info->written & (D9_OUT_T0 << i))
                    t->outputs_written |= 1u << i;
        } else {
            for (UINT r = 0; r < 12; r++) {
                if (!(info->out_declared & (1u << r)))
                    continue;
                UINT usage = info->out_usage[r], index = info->out_index[r];
                if ((usage == D3DDECLUSAGE_POSITION || usage == D3DDECLUSAGE_PSIZE) && !index)
                    continue;
                int slot = D9VaryingSlot(usage, index);
                if (slot >= 0)
                    t->outputs_written |= 1u << slot;
            }
        }
        return;
    }
    UINT wanted = 0;
    if (info->major >= 3) {
        for (UINT r = 0; r < 16; r++) {
            if (!(info->in_declared & (1u << r)))
                continue;
            int slot = D9VaryingSlot(info->in_usage[r], info->in_index[r]);
            if (slot < 0)
                continue;
            wanted |= 1u << slot;
            if (info->in_centroid[r])
                t->centroid |= 1u << slot;
        }
    } else {
        for (UINT r = 0; r < 2; r++)
            if (info->in_declared & (1u << r))
                wanted |= 1u << (8 + r);
        for (UINT r = 0; r < 8; r++)
            if (info->in_declared & (1u << (8 + r))) {
                wanted |= 1u << r;
                if (info->in_centroid[8 + r])
                    t->centroid |= 1u << r;
            }
    }
    if (key->fog == 1 && info->major < 3)
        wanted |= 1u << 10;
    t->inputs_read = wanted & key->ps_inputs;
    if (key->point_sprite)
        t->reads_point_coord = TRUE;
    t->reads_position = info->vpos;
}

HRESULT D9TranslateShader(const D9ShaderInfo *info, const D9ShaderKey *key, void **bytecode, SIZE_T *size,
                          UINT *outputs)
{
    Tx t = {info, key};
    t.next_temp = t.temp_max = 48;
    t.r_addr = temp(&t);
    t.r_pred = temp(&t);
    t.r_matrix = temp(&t);
    for (UINT i = 0; i < 16; i++)
        t.r_out[i] = temp(&t);
    t.in_main = TRUE;
    plan_registers(&t);
    D9Code *c = &t.code;
    /* the outputs start as zeros with w 1; integer attributes are read as floats. */
    if (!info->pixel) {
        for (UINT i = 0; i < 13; i++)
            D9Op1(c, OP_MOV, D9Temp(t.r_out[i], 0xF), D9Imm4(0, 0, 0, fbits(1)));
        for (UINT r = 0; r < 16; r++) {
            UINT integer = key->int_inputs >> (r * 2) & 3;
            if (!integer || !(info->in_declared & (1u << r)))
                continue;
            t.r_in[r] = temp(&t);
            D9Op1(c, integer == 1 ? OP_UTOF : OP_ITOF, D9Temp(t.r_in[r], 0xF), D9Src(OPERAND_INPUT, r, SWZ_XYZW));
        }
    } else {
        D9Op1(c, OP_MOV, D9Temp(t.r_out[0], 0xF), D9Imm4(0, 0, 0, 0));
        if (info->vpos) {
            D9Op2(c, OP_ADD, D9Temp(t.r_out[15], 3), D9Src(OPERAND_INPUT, D9_SLOT_POSITION, SWZ_XYZW), D9ImmF(-0.5f));
            D9Op1(c, OP_MOV, D9Temp(t.r_out[15], 0xC), D9Imm4(0, 0, 0, 0));
        }
        if (info->vface)
            D9Op3(c, OP_MOVC, D9Temp(t.r_out[14], 0xF), D9Src(OPERAND_INPUT, D9_SLOT_FACE, REPL(0)), D9ImmF(1.0f),
                  D9ImmF(-1.0f));
    }
    const DWORD *code = info->code + 1;
    UINT left = info->words - 1;
    D9Inst inst;
    while (!t.failed && D9Decode(info, code, left, &inst)) {
        if (inst.opcode == D9_OP_END)
            break;
        translate(&t, &inst);
        code += inst.words;
        left -= inst.words;
    }
    if (t.in_main)
        D9TranslateEpilogue(&t);
    else
        D9Emit(c, OP_RET, nullptr, 0);
    if (t.failed) {
        D9Log("d3d9: no translation of a %s shader %u.%u: %s", info->pixel ? "pixel" : "vertex", info->major,
              info->minor, t.why ? t.why : "an instruction it cannot decode");
        D9CodeFree(c);
        return E_NOTIMPL;
    }
    D9Code program = {};
    declare(&t, &program);
    for (UINT i = 0; i < c->count; i++)
        D9CodePut(&program, c->words[i]);
    if (!program.failed)
        program.words[1] = program.count;
    D9CodeFree(c);
    HRESULT hr = D9Container(&t.inputs, &t.outputs, &program, bytecode, size);
    D9CodeFree(&program);
    *outputs = t.outputs_written;
    return hr;
}

/* ------------------------------------------------------------------ parsing */

/* the outputs a vertex shader before model 3 writes, by register type and number. */
static UINT vs_output_bit(UINT type, UINT num)
{
    if (type == RT_RASTOUT && num <= 2)
        return 1u << num;
    if (type == RT_ATTROUT && num <= 1)
        return D9_OUT_D0 << num;
    if (type == RT_TEXCRDOUT && num <= 7)
        return D9_OUT_T0 << num;
    return 0;
}

HRESULT D9ParseShader(const DWORD *function, BOOL pixel, D9ShaderInfo *info)
{
    memset(info, 0, sizeof(*info));
    if (!function)
        return D3DERR_INVALIDCALL;
    DWORD version = function[0];
    if ((version >> 16) != (pixel ? 0xFFFFu : 0xFFFEu))
        return D3DERR_INVALIDCALL;
    info->pixel = pixel;
    info->major = (version >> 8) & 0xFF;
    info->minor = version & 0xFF;
    if (info->major < 1 || info->major > 3)
        return D3DERR_INVALIDCALL;
    /* the program's length: to the end token, which code programs give carries no size. */
    UINT words = 1;
    const UINT max = 1u << 20;
    D9Inst inst;
    while (words < max) {
        if (!D9Decode(info, function + words, max - words, &inst))
            return D3DERR_INVALIDCALL;
        words += inst.words;
        if (inst.opcode == D9_OP_END)
            break;
    }
    info->code = HeapAlloc(GetProcessHeap(), 0, words * sizeof(DWORD));
    if (!info->code)
        return E_OUTOFMEMORY;
    memcpy(info->code, function, words * sizeof(DWORD));
    info->words = words;
    UINT defs_max = 0;
    for (UINT at = 1; at < words;) {
        D9Decode(info, info->code + at, words - at, &inst);
        at += inst.words;
        if (inst.opcode == D9_OP_END)
            break;
        UINT dtype = D9RegType(inst.dst.token), dnum = D9RegNum(inst.dst.token);
        if (inst.opcode == D9_OP_DCL) {
            DWORD usage = inst.usage;
            UINT mask = (inst.dst.token >> 16) & 0xF;
            if (dtype == RT_SAMPLER && dnum < 16) {
                info->sampler_type[dnum] = (BYTE)((usage >> 27) & 0xF);
            } else if (dtype == RT_INPUT && (!pixel || info->major >= 3) && dnum < 16) {
                info->in_usage[dnum] = usage & 0x1F;
                info->in_index[dnum] = (usage >> 16) & 0xF;
                info->in_mask[dnum] = (BYTE)mask;
                info->in_centroid[dnum] = (inst.dst.token >> 22) & 1;
                info->in_declared |= 1u << dnum;
            } else if (pixel && dtype == RT_INPUT && dnum < 2) {
                info->in_declared |= 1u << dnum;
            } else if (pixel && dtype == RT_TEXTURE && dnum < 8) {
                info->in_declared |= 1u << (8 + dnum);
                info->in_centroid[8 + dnum] = (inst.dst.token >> 22) & 1;
            } else if (dtype == RT_OUTPUT && !pixel && dnum < 12) {
                info->out_usage[dnum] = usage & 0x1F;
                info->out_index[dnum] = (usage >> 16) & 0xF;
                info->out_mask[dnum] = (BYTE)mask;
                info->out_declared |= 1u << dnum;
            } else if (dtype == RT_MISCTYPE) {
                if (dnum == 0)
                    info->vpos = TRUE;
                else
                    info->vface = TRUE;
            }
            continue;
        }
        if (inst.opcode == D9_OP_DEF || inst.opcode == D9_OP_DEFI || inst.opcode == D9_OP_DEFB) {
            if (info->def_count == defs_max) {
                defs_max = defs_max ? defs_max * 2 : 16;
                void *defs = info->defs ? HeapReAlloc(GetProcessHeap(), 0, info->defs, defs_max * sizeof(*info->defs))
                                        : HeapAlloc(GetProcessHeap(), 0, defs_max * sizeof(*info->defs));
                if (!defs)
                    return E_OUTOFMEMORY;
                info->defs = defs;
            }
            info->defs[info->def_count].reg = dnum | dtype << 16;
            memcpy(info->defs[info->def_count].v, inst.values, sizeof(inst.values));
            info->def_count++;
            if (dtype == RT_CONST && dnum + 1 > info->float_count)
                info->float_count = dnum + 1;
            continue;
        }
        if (inst.has_dst) {
            if (!pixel && info->major < 3)
                info->written |= vs_output_bit(dtype, dnum);
            if (!pixel && info->major >= 3 && dtype == RT_OUTPUT && dnum < 12 && !(info->out_declared & (1u << dnum))) {
                info->out_declared |= 1u << dnum;
                info->out_usage[dnum] = D3DDECLUSAGE_TEXCOORD;
                info->out_index[dnum] = dnum;
            }
            if (pixel && dtype == RT_COLOROUT && dnum < 4)
                info->written |= 1u << dnum;
            if (pixel && dtype == RT_DEPTHOUT)
                info->writes_depth = TRUE;
        }
        /* matrix instructions read the rows after their constant too. */
        UINT rows = inst.opcode >= D9_OP_M4x4 && inst.opcode <= D9_OP_M3x2 ? (inst.opcode == D9_OP_M4x4 || inst.opcode == D9_OP_M3x4 ? 4 : inst.opcode == D9_OP_M3x2 ? 2 : 3) : 1;
        for (UINT i = 0; i < inst.src_count; i++) {
            UINT type = D9RegType(inst.src[i].token), num = D9RegNum(inst.src[i].token);
            if (type == RT_CONST) {
                if (inst.src[i].has_rel)
                    info->relative = TRUE;
                UINT n = num + (i == 1 ? rows : 1);
                if (n > info->float_count)
                    info->float_count = n;
            }
            if (type == RT_MISCTYPE) {
                if (num == 0)
                    info->vpos = TRUE;
                else
                    info->vface = TRUE;
            }
            if (type == RT_CONSTINT || type == RT_CONSTBOOL)
                info->uses_ints = TRUE;
            if (pixel && info->major < 3 && type == RT_TEXTURE && num < 8)
                info->in_declared |= 1u << (8 + num);
            if (pixel && info->major < 3 && type == RT_INPUT && num < 2)
                info->in_declared |= 1u << num;
        }
    }
    /* model 1 pixel shaders read every color and coordinate their stages ask for, and may write depth. */
    if (pixel && info->major < 2) {
        info->in_declared |= 0xFF03;
        if (info->code)
            for (UINT at = 1; at < words;) {
                D9Decode(info, info->code + at, words - at, &inst);
                at += inst.words;
                if (inst.opcode == D9_OP_TEXDEPTH || inst.opcode == D9_OP_TEXM3x2DEPTH)
                    info->writes_depth = TRUE;
                if (inst.opcode == D9_OP_END)
                    break;
            }
    }
    if (info->float_count > (pixel ? D9_PS_CONSTANTS : D9_VS_CONSTANTS))
        info->float_count = pixel ? D9_PS_CONSTANTS : D9_VS_CONSTANTS;
    return D3D_OK;
}

void D9FreeShaderInfo(D9ShaderInfo *info)
{
    HeapFree(GetProcessHeap(), 0, info->code);
    HeapFree(GetProcessHeap(), 0, info->defs);
    memset(info, 0, sizeof(*info));
}
