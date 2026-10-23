/*
 * a spir-v module builder: words go to the module's sections in order,
 * types and constants are made once, and functions are written a block at
 * a time.
 */
#ifndef MIME_LIB_DXBC_SPIRV_H
#define MIME_LIB_DXBC_SPIRV_H

#include <stdint.h>
#include <stddef.h>

#include "../alloc.h"
#include "spirv.h"

typedef struct SpvSection {
    uint32_t *words;
    size_t count, capacity;
} SpvSection;

enum {
    SECTION_CAPABILITIES,
    SECTION_EXTENSIONS,
    SECTION_IMPORTS,
    SECTION_ENTRY_POINTS,
    SECTION_EXECUTION_MODES,
    SECTION_DEBUG,
    SECTION_ANNOTATIONS,
    SECTION_GLOBALS,
    SECTION_FUNCTIONS,
    SECTION_COUNT
};

/* the hash of a made type or constant's words, and where they are in the globals. */
typedef struct SpvCached {
    uint32_t hash;
    uint32_t id;
    uint32_t offset;
    uint32_t count;
} SpvCached;

typedef struct SpvBuilder {
    const MimeAllocator *alloc;
    SpvSection sections[SECTION_COUNT];
    uint32_t next_id;
    uint64_t capabilities[4];
    SpvCached *cache;
    uint32_t cache_count, cache_capacity;
    uint32_t glsl;
    /* logical unless the module reads memory through device addresses. */
    uint32_t addressing;
    /* the function being written: whether the current block has its terminator, and its label. */
    int block_open;
    uint32_t block;
    int failed;
} SpvBuilder;

void spv_init(SpvBuilder *b, const MimeAllocator *alloc);
void spv_free(SpvBuilder *b);
uint32_t spv_id(SpvBuilder *b);

void spv_emit(SpvBuilder *b, int section, uint32_t op, const uint32_t *operands, uint32_t count);
void spv_emit_string(SpvBuilder *b, int section, uint32_t op, const uint32_t *before, uint32_t before_count,
                     const char *text, const uint32_t *after, uint32_t after_count);
void spv_capability(SpvBuilder *b, uint32_t capability);
void spv_extension(SpvBuilder *b, const char *name);
void spv_decorate(SpvBuilder *b, uint32_t id, uint32_t decoration, const uint32_t *args, uint32_t count);
void spv_member_decorate(SpvBuilder *b, uint32_t id, uint32_t member, uint32_t decoration, const uint32_t *args,
                         uint32_t count);
void spv_name(SpvBuilder *b, uint32_t id, const char *name);

/* types and constants, each made once. */
uint32_t spv_type_void(SpvBuilder *b);
uint32_t spv_type_bool(SpvBuilder *b);
uint32_t spv_type_int(SpvBuilder *b, uint32_t width, uint32_t signedness);
uint32_t spv_type_float(SpvBuilder *b, uint32_t width);
uint32_t spv_type_vector(SpvBuilder *b, uint32_t component, uint32_t count);
uint32_t spv_type_pointer(SpvBuilder *b, uint32_t storage, uint32_t type);
uint32_t spv_type_array(SpvBuilder *b, uint32_t element, uint32_t length);
uint32_t spv_type_runtime_array(SpvBuilder *b, uint32_t element);
uint32_t spv_type_struct(SpvBuilder *b, const uint32_t *members, uint32_t count);
uint32_t spv_type_function(SpvBuilder *b, uint32_t ret, const uint32_t *params, uint32_t count);
uint32_t spv_type_image(SpvBuilder *b, uint32_t sampled_type, uint32_t dim, uint32_t depth, uint32_t arrayed,
                        uint32_t ms, uint32_t sampled, uint32_t format);
uint32_t spv_type_sampled_image(SpvBuilder *b, uint32_t image);
uint32_t spv_type_sampler(SpvBuilder *b);
uint32_t spv_type_matrix(SpvBuilder *b, uint32_t column, uint32_t count);
/* a type without operands: acceleration structures, ray queries. */
uint32_t spv_type_opaque(SpvBuilder *b, uint32_t op);
uint32_t spv_const(SpvBuilder *b, uint32_t type, uint32_t bits);
uint32_t spv_const64(SpvBuilder *b, uint32_t type, uint64_t bits);
uint32_t spv_const_bool(SpvBuilder *b, int value);
uint32_t spv_const_composite(SpvBuilder *b, uint32_t type, const uint32_t *parts, uint32_t count);
uint32_t spv_const_null(SpvBuilder *b, uint32_t type);
/* made each time: immediate constant buffers. */
uint32_t spv_const_composite_array(SpvBuilder *b, uint32_t type, const uint32_t *parts, uint32_t count);
/* a global unique to its caller, never shared: struct types that get decorations, variables. */
uint32_t spv_type_struct_unique(SpvBuilder *b, const uint32_t *members, uint32_t count);
uint32_t spv_type_array_unique(SpvBuilder *b, uint32_t element, uint32_t length);
uint32_t spv_type_runtime_array_unique(SpvBuilder *b, uint32_t element);
uint32_t spv_variable(SpvBuilder *b, uint32_t pointer_type, uint32_t storage, uint32_t initializer);

/* instructions in the current function. */
uint32_t spv_op(SpvBuilder *b, uint32_t op, uint32_t type, const uint32_t *operands, uint32_t count);
void spv_op_void(SpvBuilder *b, uint32_t op, const uint32_t *operands, uint32_t count);
uint32_t spv_op1(SpvBuilder *b, uint32_t op, uint32_t type, uint32_t a);
uint32_t spv_op2(SpvBuilder *b, uint32_t op, uint32_t type, uint32_t a, uint32_t c);
uint32_t spv_op3(SpvBuilder *b, uint32_t op, uint32_t type, uint32_t a, uint32_t c, uint32_t d);
uint32_t spv_glsl(SpvBuilder *b, uint32_t type, uint32_t instruction, const uint32_t *args, uint32_t count);
uint32_t spv_load(SpvBuilder *b, uint32_t type, uint32_t pointer);
void spv_store(SpvBuilder *b, uint32_t pointer, uint32_t value);
uint32_t spv_access(SpvBuilder *b, uint32_t pointer_type, uint32_t base, const uint32_t *indices, uint32_t count);
void spv_label(SpvBuilder *b, uint32_t label);
void spv_branch(SpvBuilder *b, uint32_t target);
void spv_branch_conditional(SpvBuilder *b, uint32_t condition, uint32_t yes, uint32_t no);
void spv_return(SpvBuilder *b);
void spv_function_begin(SpvBuilder *b, uint32_t id, uint32_t ret, uint32_t type);
void spv_function_end(SpvBuilder *b);

/* allocated with the builder's allocator. */
uint32_t *spv_finish(SpvBuilder *b, size_t *words);

#endif
