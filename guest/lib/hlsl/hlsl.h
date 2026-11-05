#ifndef MIME_LIB_HLSL_H
#define MIME_LIB_HLSL_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

/* D3DCOMPILE_* flags, with the same values. */
enum {
    HLSL_DEBUG = 1 << 0,
    HLSL_SKIP_VALIDATION = 1 << 1,
    HLSL_SKIP_OPTIMIZATION = 1 << 2,
    HLSL_PACK_MATRIX_ROW_MAJOR = 1 << 3,
    HLSL_PACK_MATRIX_COLUMN_MAJOR = 1 << 4,
    HLSL_PARTIAL_PRECISION = 1 << 5,
    HLSL_FORCE_VS_SOFTWARE_NO_OPT = 1 << 6,
    HLSL_FORCE_PS_SOFTWARE_NO_OPT = 1 << 7,
    HLSL_NO_PRESHADER = 1 << 8,
    HLSL_AVOID_FLOW_CONTROL = 1 << 9,
    HLSL_PREFER_FLOW_CONTROL = 1 << 10,
    HLSL_ENABLE_STRICTNESS = 1 << 11,
    HLSL_ENABLE_BACKWARDS_COMPATIBILITY = 1 << 12,
    HLSL_IEEE_STRICTNESS = 1 << 13,
    HLSL_OPTIMIZATION_LEVEL0 = 1 << 14,
    HLSL_OPTIMIZATION_LEVEL3 = 1 << 15,
    HLSL_WARNINGS_ARE_ERRORS = 1 << 18,
    HLSL_RESOURCES_MAY_ALIAS = 1 << 19,
    HLSL_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES = 1 << 20,
    HLSL_ALL_RESOURCES_BOUND = 1 << 21,
    HLSL_DEBUG_NAME_FOR_SOURCE = 1 << 22,
    HLSL_DEBUG_NAME_FOR_BINARY = 1 << 23,
};

typedef struct HlslMacro {
    const char *name;
    const char *definition;
} HlslMacro;

/* ID3DInclude: open returns 0 on success; parent is the data of the including file, NULL for the source. */
typedef struct HlslInclude {
    int (*open)(void *ctx, int system, const char *name, const void *parent, const void **data, uint32_t *size);
    void (*close)(void *ctx, const void *data);
    void *ctx;
} HlslInclude;

typedef struct HlslOptions {
    const char *source;
    size_t size;
    /* the name errors give, NULL for "Shader@0x<source>". */
    const char *name;
    const HlslMacro *macros;
    uint32_t macro_count;
    const HlslInclude *include;
    const char *entry;
    const char *target;
    uint32_t flags;
    uint32_t effect_flags;
} HlslOptions;

typedef struct Hlsl Hlsl;

/*
 * fatal is called on exhausted memory and internal errors and must not
 * return: the caller unwinds and still calls hlsl_delete, which frees what
 * the compiler allocated.
 */
Hlsl *hlsl_new(const MimeAllocator *alloc, void (*fatal)(void *ctx), void *fatal_ctx);
void hlsl_delete(Hlsl *h);

/* 0 on success; the output is the container, or the text for hlsl_preprocess. */
int hlsl_compile(Hlsl *h, const HlslOptions *options);
int hlsl_preprocess(Hlsl *h, const HlslOptions *options);
const void *hlsl_output(Hlsl *h, size_t *size);
/* the errors and warnings, NULL when there are none. */
const char *hlsl_messages(Hlsl *h, size_t *size);

#endif
