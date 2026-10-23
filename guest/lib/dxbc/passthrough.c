/*
 * the geometry shader direct3d implies when stream output comes from a
 * vertex or domain shader: each vertex of a primitive passes through
 * unchanged and the primitive is cut after it. it is built as its own
 * container, with the source's outputs as inputs and outputs, so it
 * compiles like any geometry shader.
 */
#include "dxbc.h"
#include "tokens.h"

typedef struct Writer {
    uint8_t *data;
    size_t size, capacity;
    const MimeAllocator *alloc;
    int failed;
} Writer;

static void put(Writer *w, const void *bytes, size_t n)
{
    if (w->failed)
        return;
    if (w->size + n > w->capacity) {
        size_t capacity = w->capacity ? w->capacity * 2 : 1024;
        while (capacity < w->size + n)
            capacity *= 2;
        uint8_t *data = mime_alloc(w->alloc, capacity);
        if (!data) {
            w->failed = 1;
            return;
        }
        if (w->data)
            __builtin_memcpy(data, w->data, w->size);
        mime_free(w->alloc, w->data);
        w->data = data;
        w->capacity = capacity;
    }
    __builtin_memcpy(w->data + w->size, bytes, n);
    w->size += n;
}

static void put32(Writer *w, uint32_t v) { put(w, &v, 4); }

static void set32(Writer *w, size_t at, uint32_t v)
{
    if (!w->failed)
        __builtin_memcpy(w->data + at, &v, 4);
}

/* the elements, then their names. */
static void put_signature(Writer *w, const DxbcSignature *sig)
{
    size_t start = w->size;
    put32(w, sig->count);
    put32(w, 8);
    uint32_t names = 8 + sig->count * 24;
    for (uint32_t i = 0; i < sig->count; i++) {
        const DxbcSignatureElement *e = &sig->elements[i];
        uint32_t words[6] = {names, e->semantic_index, e->system_value, e->component_type, e->reg,
                             e->mask | (uint32_t)e->mask << 8};
        put(w, words, sizeof(words));
        names += (uint32_t)__builtin_strlen(e->name) + 1;
    }
    for (uint32_t i = 0; i < sig->count; i++)
        put(w, sig->elements[i].name, __builtin_strlen(sig->elements[i].name) + 1);
    while ((w->size - start) & 3)
        put(w, "", 1);
}

/* the operand tokens of v[vertex][register] and o[register], with four components. */
#define INPUT_MASK(mask) (2u | SELECT_MASK << 2 | (uint32_t)(mask) << 4 | OPERAND_INPUT << 12 | 2u << 20)
#define INPUT_XYZW (2u | SELECT_SWIZZLE << 2 | 0xE4u << 4 | OPERAND_INPUT << 12 | 2u << 20)
#define OUTPUT_MASK(mask) (2u | SELECT_MASK << 2 | (uint32_t)(mask) << 4 | OPERAND_OUTPUT << 12 | 1u << 20)

static void put_code(Writer *w, const DxbcSignature *sig, uint32_t primitive)
{
    static const uint32_t VERTICES[8] = {0, 1, 2, 3, 0, 0, 4, 6};
    static const uint8_t PASSED[8][3] = {{0}, {0}, {0, 1}, {0, 1, 2}, {0}, {0}, {1, 2}, {0, 2, 4}};
    static const uint8_t PASSED_COUNT[8] = {0, 1, 2, 3, 0, 0, 2, 3};
    static const uint32_t TOPOLOGY[4] = {0, 1, 3, 5};
    uint32_t vertices = VERTICES[primitive & 7];
    uint32_t passed = PASSED_COUNT[primitive & 7];
    /* registers the outputs fill, with their components and system value. */
    uint8_t masks[32] = {};
    uint32_t svs[32] = {};
    for (uint32_t i = 0; i < sig->count; i++) {
        const DxbcSignatureElement *e = &sig->elements[i];
        if (e->reg >= 32)
            continue;
        masks[e->reg] |= e->mask;
        if (e->system_value)
            svs[e->reg] = e->system_value;
    }
    size_t start = w->size;
    put32(w, (uint32_t)DXBC_GEOMETRY << 16 | 4 << 4);
    put32(w, 0);
    put32(w, OP_DCL_GS_INPUT_PRIMITIVE | primitive << 11 | 1 << 24);
    put32(w, OP_DCL_GS_OUTPUT_TOPOLOGY | TOPOLOGY[passed] << 11 | 1 << 24);
    for (uint32_t reg = 0; reg < 32; reg++) {
        if (!masks[reg])
            continue;
        uint32_t siv = svs[reg] != 0;
        put32(w, (siv ? OP_DCL_INPUT_SIV : OP_DCL_INPUT) | (4 + siv) << 24);
        put32(w, INPUT_MASK(masks[reg]));
        put32(w, vertices);
        put32(w, reg);
        if (siv)
            put32(w, svs[reg]);
        put32(w, (siv ? OP_DCL_OUTPUT_SIV : OP_DCL_OUTPUT) | (3 + siv) << 24);
        put32(w, OUTPUT_MASK(masks[reg]));
        put32(w, reg);
        if (siv)
            put32(w, svs[reg]);
    }
    put32(w, OP_DCL_MAX_OUTPUT_VERTEX_COUNT | 2 << 24);
    put32(w, passed);
    for (uint32_t v = 0; v < passed; v++) {
        for (uint32_t reg = 0; reg < 32; reg++) {
            if (!masks[reg])
                continue;
            put32(w, OP_MOV | 6 << 24);
            put32(w, OUTPUT_MASK(masks[reg]));
            put32(w, reg);
            put32(w, INPUT_XYZW);
            put32(w, PASSED[primitive & 7][v]);
            put32(w, reg);
        }
        put32(w, OP_EMIT | 1 << 24);
    }
    put32(w, OP_CUT | 1 << 24);
    put32(w, OP_RET | 1 << 24);
    set32(w, start + 4, (uint32_t)(w->size - start) / 4);
}

void *dxbc_passthrough_gs(const void *bytecode, size_t length, uint32_t primitive, const MimeAllocator *alloc,
                          size_t *out_length)
{
    static const uint32_t TAGS[3] = {DXBC_FOURCC('I', 'S', 'G', 'N'), DXBC_FOURCC('O', 'S', 'G', 'N'),
                                     DXBC_FOURCC('S', 'H', 'E', 'X')};
    DxbcSignature sig;
    if (dxbc_parse_io(bytecode, length, 1, &sig, alloc))
        return nullptr;
    Writer w = {0, 0, 0, alloc, 0};
    put(&w, "DXBC", 4);
    for (int i = 0; i < 4; i++)
        put32(&w, 0);
    put32(&w, 1);
    put32(&w, 0);
    put32(&w, 3);
    size_t offsets = w.size;
    for (int i = 0; i < 3; i++)
        put32(&w, 0);
    for (int i = 0; i < 3; i++) {
        set32(&w, offsets + i * 4, (uint32_t)w.size);
        put32(&w, TAGS[i]);
        size_t size_at = w.size;
        put32(&w, 0);
        if (i < 2)
            put_signature(&w, &sig);
        else
            put_code(&w, &sig, primitive);
        set32(&w, size_at, (uint32_t)(w.size - size_at - 4));
    }
    mime_free(alloc, sig.elements);
    if (w.failed) {
        mime_free(alloc, w.data);
        return nullptr;
    }
    set32(&w, 24, (uint32_t)w.size);
    uint32_t sum[4];
    dxbc_checksum(w.data, w.size, sum);
    __builtin_memcpy(w.data + 4, sum, 16);
    *out_length = w.size;
    return w.data;
}
