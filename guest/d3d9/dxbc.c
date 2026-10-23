/* a shader model 4 bytecode writer for the shaders d3d9.dll translates, with the container d3d11 takes. */
#include "d3d9_private.h"
#include "../lib/dxbc/tokens.h"
#include "../lib/dxbc/dxbc.h"

void D9CodePut(D9Code *c, DWORD word)
{
    if (c->failed)
        return;
    if (c->count == c->capacity) {
        UINT capacity = c->capacity ? c->capacity * 2 : 1024;
        DWORD *words = c->words ? HeapReAlloc(GetProcessHeap(), 0, c->words, capacity * sizeof(DWORD))
                                : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(DWORD));
        if (!words) {
            c->failed = TRUE;
            return;
        }
        c->words = words;
        c->capacity = capacity;
    }
    c->words[c->count++] = word;
}

void D9CodeFree(D9Code *c)
{
    HeapFree(GetProcessHeap(), 0, c->words);
    memset(c, 0, sizeof(*c));
}

static D9Opnd operand(DWORD token, UINT type, UINT dims)
{
    D9Opnd o = {{token | type << 12 | dims << 20}, 1};
    return o;
}

static DWORD mask_token(UINT mask) { return 2 | SELECT_MASK << 2 | (mask & 0xF) << 4; }

static DWORD swizzle_token(UINT swizzle) { return 2 | SELECT_SWIZZLE << 2 | (swizzle & 0xFF) << 4; }

static D9Opnd indexed(DWORD token, UINT type, UINT index)
{
    D9Opnd o = operand(token, type, 1);
    o.words[o.count++] = index;
    return o;
}

D9Opnd D9Dst(UINT type, UINT reg, UINT mask) { return indexed(mask_token(mask), type, reg); }

D9Opnd D9Src(UINT type, UINT reg, UINT swizzle) { return indexed(swizzle_token(swizzle), type, reg); }

D9Opnd D9Temp(UINT reg, UINT mask) { return D9Dst(OPERAND_TEMP, reg, mask); }

D9Opnd D9TempSrc(UINT reg, UINT swizzle) { return D9Src(OPERAND_TEMP, reg, swizzle); }

/* a single temp component, as conditions and indices take it. */
D9Opnd D9TempScalar(UINT reg, UINT component)
{
    D9Opnd o = operand(2 | SELECT_SELECT1 << 2 | (component & 3) << 4, OPERAND_TEMP, 1);
    o.words[o.count++] = reg;
    return o;
}

D9Opnd D9Imm4(DWORD x, DWORD y, DWORD z, DWORD w)
{
    D9Opnd o = operand(2, OPERAND_IMMEDIATE32, 0);
    o.words[o.count++] = x;
    o.words[o.count++] = y;
    o.words[o.count++] = z;
    o.words[o.count++] = w;
    return o;
}

D9Opnd D9Imm(DWORD v)
{
    D9Opnd o = operand(1, OPERAND_IMMEDIATE32, 0);
    o.words[o.count++] = v;
    return o;
}

D9Opnd D9ImmF(float f)
{
    DWORD v;
    memcpy(&v, &f, 4);
    return D9Imm4(v, v, v, v);
}

/* cb[slot][element], or cb[slot][element + a temp's component]. */
D9Opnd D9Cb(UINT slot, UINT element, UINT swizzle, int rel_reg, UINT rel_component)
{
    DWORD token = swizzle_token(swizzle) | OPERAND_CONSTANT_BUFFER << 12 | 2u << 20;
    if (rel_reg >= 0)
        token |= (DWORD)INDEX_IMMEDIATE32_RELATIVE << 25;
    D9Opnd o = {{token, slot, element}, 3};
    if (rel_reg >= 0) {
        D9Opnd r = D9TempScalar((UINT)rel_reg, rel_component);
        for (UINT i = 0; i < r.count; i++)
            o.words[o.count++] = r.words[i];
    }
    return o;
}

D9Opnd D9ResourceOpnd(UINT reg) { return indexed(swizzle_token(0xE4), OPERAND_RESOURCE, reg); }

D9Opnd D9SamplerOpnd(UINT reg) { return indexed(0, OPERAND_SAMPLER, reg); }

/* a source with its modifier: negated, absolute, or both. */
D9Opnd D9Mod(D9Opnd o, UINT modifier)
{
    if (!modifier)
        return o;
    if (o.words[0] & 0x80000000u) {
        o.words[1] |= modifier << 6;
        return o;
    }
    o.words[0] |= 0x80000000u;
    for (UINT i = o.count; i > 1; i--)
        o.words[i] = o.words[i - 1];
    o.words[1] = 1 | modifier << 6;
    o.count++;
    return o;
}

D9Opnd D9Neg(D9Opnd o) { return D9Mod(o, MOD_NEG); }

D9Opnd D9Abs(D9Opnd o) { return D9Mod(o, MOD_ABS); }

void D9Emit(D9Code *c, DWORD opcode, const D9Opnd *ops, UINT count)
{
    UINT length = 1;
    for (UINT i = 0; i < count; i++)
        length += ops[i].count;
    D9CodePut(c, opcode | length << 24);
    for (UINT i = 0; i < count; i++)
        for (UINT j = 0; j < ops[i].count; j++)
            D9CodePut(c, ops[i].words[j]);
}

void D9Op1(D9Code *c, DWORD op, D9Opnd d, D9Opnd a)
{
    D9Opnd ops[2] = {d, a};
    D9Emit(c, op, ops, 2);
}

void D9Op2(D9Code *c, DWORD op, D9Opnd d, D9Opnd a, D9Opnd b)
{
    D9Opnd ops[3] = {d, a, b};
    D9Emit(c, op, ops, 3);
}

void D9Op3(D9Code *c, DWORD op, D9Opnd d, D9Opnd a, D9Opnd b, D9Opnd e)
{
    D9Opnd ops[4] = {d, a, b, e};
    D9Emit(c, op, ops, 4);
}

static void put_bytes(D9Code *c, const void *bytes, UINT n)
{
    DWORD word = 0;
    const BYTE *b = bytes;
    for (UINT i = 0; i < n; i++) {
        word |= (DWORD)b[i] << ((i & 3) * 8);
        if ((i & 3) == 3) {
            D9CodePut(c, word);
            word = 0;
        }
    }
    if (n & 3)
        D9CodePut(c, word);
}

static void put_signature(D9Code *c, const D9Signature *sig)
{
    UINT start = c->count;
    D9CodePut(c, sig->count);
    D9CodePut(c, 8);
    UINT names = 8 + sig->count * 24;
    for (UINT i = 0; i < sig->count; i++) {
        const D9SigElement *e = &sig->elements[i];
        D9CodePut(c, names);
        D9CodePut(c, e->index);
        D9CodePut(c, e->system_value);
        D9CodePut(c, e->component_type);
        D9CodePut(c, e->reg);
        D9CodePut(c, e->mask | (DWORD)e->used << 8);
        names += lstrlenA(e->name) + 1;
    }
    /* the names packed one after another, then padded to a word. */
    BYTE *text = HeapAlloc(GetProcessHeap(), 0, D9_MAX(names, 1));
    if (!text) {
        c->failed = TRUE;
        return;
    }
    UINT at = 0;
    for (UINT i = 0; i < sig->count; i++) {
        UINT n = lstrlenA(sig->elements[i].name) + 1;
        memcpy(text + at, sig->elements[i].name, n);
        at += n;
    }
    put_bytes(c, text, at);
    HeapFree(GetProcessHeap(), 0, text);
    (void)start;
}

/* signatures and code in a container, with the checksum d3d11 checks. */
HRESULT D9Container(const D9Signature *inputs, const D9Signature *outputs, const D9Code *code, void **out,
                    SIZE_T *size)
{
    static const DWORD TAGS[3] = {MAKEFOURCC('I', 'S', 'G', 'N'), MAKEFOURCC('O', 'S', 'G', 'N'),
                                  MAKEFOURCC('S', 'H', 'E', 'X')};
    D9Code c = {};
    D9CodePut(&c, MAKEFOURCC('D', 'X', 'B', 'C'));
    for (UINT i = 0; i < 4; i++)
        D9CodePut(&c, 0);
    D9CodePut(&c, 1);
    D9CodePut(&c, 0);
    D9CodePut(&c, 3);
    UINT offsets = c.count;
    for (UINT i = 0; i < 3; i++)
        D9CodePut(&c, 0);
    for (UINT i = 0; i < 3; i++) {
        if (!c.failed)
            c.words[offsets + i] = c.count * 4;
        D9CodePut(&c, TAGS[i]);
        UINT size_at = c.count;
        D9CodePut(&c, 0);
        if (i == 0)
            put_signature(&c, inputs);
        else if (i == 1)
            put_signature(&c, outputs);
        else
            for (UINT j = 0; j < code->count; j++)
                D9CodePut(&c, code->words[j]);
        if (!c.failed)
            c.words[size_at] = (c.count - size_at - 1) * 4;
    }
    if (c.failed || code->failed) {
        D9CodeFree(&c);
        return E_OUTOFMEMORY;
    }
    c.words[6] = c.count * 4;
    dxbc_checksum(c.words, c.count * 4, (uint32_t *)&c.words[1]);
    *out = c.words;
    *size = c.count * 4;
    return D3D_OK;
}
