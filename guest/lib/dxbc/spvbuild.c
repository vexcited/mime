/*
 * the spir-v builder. sections grow as words are appended; types and
 * constants are looked up by a hash of their operand words before they
 * are made, so each exists once as spir-v requires of most types.
 */
#include "spvbuild.h"

#include "GLSL.std.450.h"

static void grow(SpvBuilder *b, SpvSection *s, size_t more)
{
    if (b->failed || s->count + more <= s->capacity)
        return;
    size_t capacity = s->capacity ? s->capacity * 2 : 256;
    while (capacity < s->count + more)
        capacity *= 2;
    uint32_t *words = mime_alloc(b->alloc, capacity * 4);
    if (!words) {
        b->failed = 1;
        return;
    }
    if (s->words) {
        __builtin_memcpy(words, s->words, s->count * 4);
        mime_free(b->alloc, s->words);
    }
    s->words = words;
    s->capacity = capacity;
}

static void put(SpvBuilder *b, int section, uint32_t word)
{
    SpvSection *s = &b->sections[section];
    grow(b, s, 1);
    if (!b->failed)
        s->words[s->count++] = word;
}

void spv_init(SpvBuilder *b, const MimeAllocator *alloc)
{
    __builtin_memset(b, 0, sizeof(*b));
    b->alloc = alloc;
    b->next_id = 1;
}

void spv_free(SpvBuilder *b)
{
    for (int i = 0; i < SECTION_COUNT; i++)
        mime_free(b->alloc, b->sections[i].words);
    mime_free(b->alloc, b->cache);
    __builtin_memset(b->sections, 0, sizeof(b->sections));
    b->cache = NULL;
}

uint32_t spv_id(SpvBuilder *b) { return b->next_id++; }

void spv_emit(SpvBuilder *b, int section, uint32_t op, const uint32_t *operands, uint32_t count)
{
    put(b, section, (count + 1) << 16 | op);
    for (uint32_t i = 0; i < count; i++)
        put(b, section, operands[i]);
}

/* the words of a nul terminated string, padded with zeros. */
static uint32_t string_words(const char *text) { return (uint32_t)(__builtin_strlen(text) / 4 + 1); }

static void put_string(SpvBuilder *b, int section, const char *text)
{
    size_t n = __builtin_strlen(text);
    for (size_t i = 0; i <= n; i += 4) {
        uint32_t word = 0;
        for (size_t k = 0; k < 4 && i + k < n; k++)
            word |= (uint32_t)(uint8_t)text[i + k] << (8 * k);
        put(b, section, word);
    }
}

void spv_emit_string(SpvBuilder *b, int section, uint32_t op, const uint32_t *before, uint32_t before_count,
                     const char *text, const uint32_t *after, uint32_t after_count)
{
    put(b, section, (1 + before_count + string_words(text) + after_count) << 16 | op);
    for (uint32_t i = 0; i < before_count; i++)
        put(b, section, before[i]);
    put_string(b, section, text);
    for (uint32_t i = 0; i < after_count; i++)
        put(b, section, after[i]);
}

void spv_capability(SpvBuilder *b, uint32_t capability)
{
    /* the common capabilities are below 256: remembered in a bitmap; others are rare enough to repeat. */
    if (capability < 256) {
        uint64_t bit = 1ull << (capability & 63);
        if (b->capabilities[capability >> 6] & bit)
            return;
        b->capabilities[capability >> 6] |= bit;
    } else {
        SpvSection *s = &b->sections[SECTION_CAPABILITIES];
        for (size_t i = 0; i + 1 < s->count; i += 2)
            if (s->words[i + 1] == capability)
                return;
    }
    spv_emit(b, SECTION_CAPABILITIES, SpvOpCapability, &capability, 1);
}

void spv_extension(SpvBuilder *b, const char *name)
{
    spv_emit_string(b, SECTION_EXTENSIONS, SpvOpExtension, NULL, 0, name, NULL, 0);
}

void spv_decorate(SpvBuilder *b, uint32_t id, uint32_t decoration, const uint32_t *args, uint32_t count)
{
    put(b, SECTION_ANNOTATIONS, (3 + count) << 16 | SpvOpDecorate);
    put(b, SECTION_ANNOTATIONS, id);
    put(b, SECTION_ANNOTATIONS, decoration);
    for (uint32_t i = 0; i < count; i++)
        put(b, SECTION_ANNOTATIONS, args[i]);
}

void spv_member_decorate(SpvBuilder *b, uint32_t id, uint32_t member, uint32_t decoration, const uint32_t *args,
                         uint32_t count)
{
    put(b, SECTION_ANNOTATIONS, (4 + count) << 16 | SpvOpMemberDecorate);
    put(b, SECTION_ANNOTATIONS, id);
    put(b, SECTION_ANNOTATIONS, member);
    put(b, SECTION_ANNOTATIONS, decoration);
    for (uint32_t i = 0; i < count; i++)
        put(b, SECTION_ANNOTATIONS, args[i]);
}

void spv_name(SpvBuilder *b, uint32_t id, const char *name)
{
    spv_emit_string(b, SECTION_DEBUG, SpvOpName, &id, 1, name, NULL, 0);
}

/* ------------------------------------------------------------------ types and constants, made once */

static uint32_t hash_words(uint32_t op, const uint32_t *words, uint32_t count)
{
    uint32_t h = 2166136261u ^ op;
    for (uint32_t i = 0; i < count; i++) {
        h ^= words[i];
        h *= 16777619u;
    }
    return h ? h : 1;
}

/*
 * the global made of op and its operands, the result id inserted at
 * result_at among them; made the first time and found afterwards.
 */
static uint32_t cached(SpvBuilder *b, uint32_t op, uint32_t result_at, const uint32_t *operands, uint32_t count)
{
    uint32_t h = hash_words(op, operands, count);
    SpvSection *g = &b->sections[SECTION_GLOBALS];
    if (b->cache_capacity) {
        uint32_t mask = b->cache_capacity - 1;
        for (uint32_t i = h & mask; b->cache[i].hash; i = (i + 1) & mask) {
            SpvCached *c = &b->cache[i];
            if (c->hash != h || c->count != count + 2)
                continue;
            const uint32_t *w = g->words + c->offset;
            if (w[0] != ((count + 2) << 16 | op))
                continue;
            int same = 1;
            for (uint32_t k = 0, j = 1; k < count && same; k++, j++) {
                if (j == 1 + result_at)
                    j++;
                same = w[j] == operands[k];
            }
            if (same)
                return c->id;
        }
    }
    if ((b->cache_count + 1) * 2 > b->cache_capacity) {
        uint32_t capacity = b->cache_capacity ? b->cache_capacity * 2 : 256;
        SpvCached *table = mime_alloc(b->alloc, capacity * sizeof(*table));
        if (!table) {
            b->failed = 1;
            return 0;
        }
        __builtin_memset(table, 0, capacity * sizeof(*table));
        for (uint32_t i = 0; i < b->cache_capacity; i++) {
            SpvCached *c = &b->cache[i];
            if (!c->hash)
                continue;
            uint32_t j = c->hash & (capacity - 1);
            while (table[j].hash)
                j = (j + 1) & (capacity - 1);
            table[j] = *c;
        }
        mime_free(b->alloc, b->cache);
        b->cache = table;
        b->cache_capacity = capacity;
    }
    uint32_t id = spv_id(b);
    uint32_t offset = (uint32_t)g->count;
    put(b, SECTION_GLOBALS, (count + 2) << 16 | op);
    for (uint32_t k = 0; k <= count; k++) {
        if (k == result_at)
            put(b, SECTION_GLOBALS, id);
        if (k < count)
            put(b, SECTION_GLOBALS, operands[k]);
    }
    uint32_t mask = b->cache_capacity - 1;
    uint32_t i = h & mask;
    while (b->cache[i].hash)
        i = (i + 1) & mask;
    b->cache[i] = (SpvCached){h, id, offset, count + 2};
    b->cache_count++;
    return id;
}

static uint32_t unique(SpvBuilder *b, uint32_t op, uint32_t result_at, const uint32_t *operands, uint32_t count)
{
    uint32_t id = spv_id(b);
    put(b, SECTION_GLOBALS, (count + 2) << 16 | op);
    for (uint32_t k = 0; k <= count; k++) {
        if (k == result_at)
            put(b, SECTION_GLOBALS, id);
        if (k < count)
            put(b, SECTION_GLOBALS, operands[k]);
    }
    return id;
}

uint32_t spv_type_void(SpvBuilder *b) { return cached(b, SpvOpTypeVoid, 0, NULL, 0); }

uint32_t spv_type_bool(SpvBuilder *b) { return cached(b, SpvOpTypeBool, 0, NULL, 0); }

uint32_t spv_type_int(SpvBuilder *b, uint32_t width, uint32_t signedness)
{
    uint32_t w[2] = {width, signedness};
    return cached(b, SpvOpTypeInt, 0, w, 2);
}

uint32_t spv_type_float(SpvBuilder *b, uint32_t width) { return cached(b, SpvOpTypeFloat, 0, &width, 1); }

uint32_t spv_type_vector(SpvBuilder *b, uint32_t component, uint32_t count)
{
    if (count == 1)
        return component;
    uint32_t w[2] = {component, count};
    return cached(b, SpvOpTypeVector, 0, w, 2);
}

uint32_t spv_type_pointer(SpvBuilder *b, uint32_t storage, uint32_t type)
{
    uint32_t w[2] = {storage, type};
    return cached(b, SpvOpTypePointer, 0, w, 2);
}

uint32_t spv_type_array(SpvBuilder *b, uint32_t element, uint32_t length)
{
    uint32_t w[2] = {element, spv_const(b, spv_type_int(b, 32, 0), length)};
    return cached(b, SpvOpTypeArray, 0, w, 2);
}

uint32_t spv_type_array_unique(SpvBuilder *b, uint32_t element, uint32_t length)
{
    uint32_t w[2] = {element, spv_const(b, spv_type_int(b, 32, 0), length)};
    return unique(b, SpvOpTypeArray, 0, w, 2);
}

uint32_t spv_type_runtime_array(SpvBuilder *b, uint32_t element)
{
    return cached(b, SpvOpTypeRuntimeArray, 0, &element, 1);
}

uint32_t spv_type_runtime_array_unique(SpvBuilder *b, uint32_t element)
{
    return unique(b, SpvOpTypeRuntimeArray, 0, &element, 1);
}

uint32_t spv_type_struct(SpvBuilder *b, const uint32_t *members, uint32_t count)
{
    return cached(b, SpvOpTypeStruct, 0, members, count);
}

uint32_t spv_type_struct_unique(SpvBuilder *b, const uint32_t *members, uint32_t count)
{
    return unique(b, SpvOpTypeStruct, 0, members, count);
}

uint32_t spv_type_function(SpvBuilder *b, uint32_t ret, const uint32_t *params, uint32_t count)
{
    uint32_t w[8];
    w[0] = ret;
    for (uint32_t i = 0; i < count && i < 7; i++)
        w[1 + i] = params[i];
    return cached(b, SpvOpTypeFunction, 0, w, count + 1);
}

uint32_t spv_type_image(SpvBuilder *b, uint32_t sampled_type, uint32_t dim, uint32_t depth, uint32_t arrayed,
                        uint32_t ms, uint32_t sampled, uint32_t format)
{
    uint32_t w[7] = {sampled_type, dim, depth, arrayed, ms, sampled, format};
    return cached(b, SpvOpTypeImage, 0, w, 7);
}

uint32_t spv_type_sampled_image(SpvBuilder *b, uint32_t image)
{
    return cached(b, SpvOpTypeSampledImage, 0, &image, 1);
}

uint32_t spv_type_sampler(SpvBuilder *b) { return cached(b, SpvOpTypeSampler, 0, NULL, 0); }
uint32_t spv_type_matrix(SpvBuilder *b, uint32_t column, uint32_t count)
{
    uint32_t w[2] = {column, count};
    return cached(b, SpvOpTypeMatrix, 0, w, 2);
}

uint32_t spv_type_opaque(SpvBuilder *b, uint32_t op) { return cached(b, op, 0, NULL, 0); }

uint32_t spv_const(SpvBuilder *b, uint32_t type, uint32_t bits)
{
    uint32_t w[2] = {type, bits};
    return cached(b, SpvOpConstant, 1, w, 2);
}

uint32_t spv_const64(SpvBuilder *b, uint32_t type, uint64_t bits)
{
    uint32_t w[3] = {type, (uint32_t)bits, (uint32_t)(bits >> 32)};
    return cached(b, SpvOpConstant, 1, w, 3);
}

uint32_t spv_const_bool(SpvBuilder *b, int value)
{
    uint32_t type = spv_type_bool(b);
    return cached(b, value ? SpvOpConstantTrue : SpvOpConstantFalse, 1, &type, 1);
}

uint32_t spv_const_composite(SpvBuilder *b, uint32_t type, const uint32_t *parts, uint32_t count)
{
    uint32_t w[17];
    w[0] = type;
    for (uint32_t i = 0; i < count && i < 16; i++)
        w[1 + i] = parts[i];
    return cached(b, SpvOpConstantComposite, 1, w, count + 1);
}

uint32_t spv_const_null(SpvBuilder *b, uint32_t type) { return cached(b, SpvOpConstantNull, 1, &type, 1); }

uint32_t spv_const_composite_array(SpvBuilder *b, uint32_t type, const uint32_t *parts, uint32_t count)
{
    uint32_t id = spv_id(b);
    put(b, SECTION_GLOBALS, (count + 3) << 16 | SpvOpConstantComposite);
    put(b, SECTION_GLOBALS, type);
    put(b, SECTION_GLOBALS, id);
    for (uint32_t i = 0; i < count; i++)
        put(b, SECTION_GLOBALS, parts[i]);
    return id;
}

uint32_t spv_variable(SpvBuilder *b, uint32_t pointer_type, uint32_t storage, uint32_t initializer)
{
    uint32_t w[3] = {pointer_type, storage, initializer};
    return unique(b, SpvOpVariable, 1, w, initializer ? 3 : 2);
}

/* ------------------------------------------------------------------ functions */

uint32_t spv_op(SpvBuilder *b, uint32_t op, uint32_t type, const uint32_t *operands, uint32_t count)
{
    uint32_t id = spv_id(b);
    put(b, SECTION_FUNCTIONS, (count + 3) << 16 | op);
    put(b, SECTION_FUNCTIONS, type);
    put(b, SECTION_FUNCTIONS, id);
    for (uint32_t i = 0; i < count; i++)
        put(b, SECTION_FUNCTIONS, operands[i]);
    return id;
}

void spv_op_void(SpvBuilder *b, uint32_t op, const uint32_t *operands, uint32_t count)
{
    spv_emit(b, SECTION_FUNCTIONS, op, operands, count);
}

uint32_t spv_op1(SpvBuilder *b, uint32_t op, uint32_t type, uint32_t a) { return spv_op(b, op, type, &a, 1); }

uint32_t spv_op2(SpvBuilder *b, uint32_t op, uint32_t type, uint32_t a, uint32_t c)
{
    uint32_t w[2] = {a, c};
    return spv_op(b, op, type, w, 2);
}

uint32_t spv_op3(SpvBuilder *b, uint32_t op, uint32_t type, uint32_t a, uint32_t c, uint32_t d)
{
    uint32_t w[3] = {a, c, d};
    return spv_op(b, op, type, w, 3);
}

uint32_t spv_glsl(SpvBuilder *b, uint32_t type, uint32_t instruction, const uint32_t *args, uint32_t count)
{
    if (!b->glsl) {
        b->glsl = spv_id(b);
        spv_emit_string(b, SECTION_IMPORTS, SpvOpExtInstImport, &b->glsl, 1, "GLSL.std.450", NULL, 0);
    }
    uint32_t w[8] = {b->glsl, instruction};
    for (uint32_t i = 0; i < count && i < 6; i++)
        w[2 + i] = args[i];
    return spv_op(b, SpvOpExtInst, type, w, count + 2);
}

uint32_t spv_load(SpvBuilder *b, uint32_t type, uint32_t pointer) { return spv_op1(b, SpvOpLoad, type, pointer); }

void spv_store(SpvBuilder *b, uint32_t pointer, uint32_t value)
{
    uint32_t w[2] = {pointer, value};
    spv_op_void(b, SpvOpStore, w, 2);
}

uint32_t spv_access(SpvBuilder *b, uint32_t pointer_type, uint32_t base, const uint32_t *indices, uint32_t count)
{
    uint32_t w[8] = {base};
    for (uint32_t i = 0; i < count && i < 7; i++)
        w[1 + i] = indices[i];
    return spv_op(b, SpvOpAccessChain, pointer_type, w, count + 1);
}

/* starts a block; a block left open before it falls through with a branch. */
void spv_label(SpvBuilder *b, uint32_t label)
{
    if (b->block_open)
        spv_branch(b, label);
    spv_op_void(b, SpvOpLabel, &label, 1);
    b->block_open = 1;
    b->block = label;
}

void spv_branch(SpvBuilder *b, uint32_t target)
{
    spv_op_void(b, SpvOpBranch, &target, 1);
    b->block_open = 0;
}

void spv_branch_conditional(SpvBuilder *b, uint32_t condition, uint32_t yes, uint32_t no)
{
    uint32_t w[3] = {condition, yes, no};
    spv_op_void(b, SpvOpBranchConditional, w, 3);
    b->block_open = 0;
}

void spv_return(SpvBuilder *b)
{
    spv_op_void(b, SpvOpReturn, NULL, 0);
    b->block_open = 0;
}

void spv_function_begin(SpvBuilder *b, uint32_t id, uint32_t ret, uint32_t type)
{
    uint32_t w[4] = {ret, id, SpvFunctionControlMaskNone, type};
    spv_emit(b, SECTION_FUNCTIONS, SpvOpFunction, w, 4);
    b->block_open = 0;
}

void spv_function_end(SpvBuilder *b)
{
    if (b->block_open)
        spv_return(b);
    spv_emit(b, SECTION_FUNCTIONS, SpvOpFunctionEnd, NULL, 0);
}

uint32_t *spv_finish(SpvBuilder *b, size_t *words)
{
    size_t total = 5;
    for (int i = 0; i < SECTION_COUNT; i++)
        total += b->sections[i].count;
    /* the memory model goes between the imports and the entry points. */
    total += 3;
    if (b->failed)
        return NULL;
    uint32_t *out = mime_alloc(b->alloc, total * 4);
    if (!out)
        return NULL;
    size_t at = 0;
    out[at++] = SpvMagicNumber;
    out[at++] = 0x00010600;
    out[at++] = 0x4D494D45; /* the generator: mime */
    out[at++] = b->next_id;
    out[at++] = 0;
    for (int i = 0; i < SECTION_COUNT; i++) {
        if (i == SECTION_ENTRY_POINTS) {
            out[at++] = 3 << 16 | SpvOpMemoryModel;
            out[at++] = b->addressing;
            out[at++] = SpvMemoryModelGLSL450;
        }
        __builtin_memcpy(out + at, b->sections[i].words, b->sections[i].count * 4);
        at += b->sections[i].count;
    }
    *words = at;
    return out;
}
