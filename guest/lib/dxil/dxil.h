/*
 * dxil: direct3d 12's shader model 6 bytecode, an llvm 3.7 module in a dxbc
 * container. it compiles to spir-v with the same binding model, options and
 * results as dxbc_compile.
 */
#ifndef MIME_LIB_DXIL_H
#define MIME_LIB_DXIL_H

#include "../dxbc/dxbc.h"

/* whether the container holds dxil rather than dxbc. */
int dxil_is(const void *bytecode, size_t length);

/*
 * compiles the shader, or the library function options->export_name names.
 * 0 on success, the reason in info->error otherwise. free with dxbc_free.
 */
int dxil_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc);

/* a library export: name, shader kind as dxil numbers them, payload and attribute sizes. */
typedef struct DxilExport {
    char name[128];
    uint32_t kind;
    uint32_t payload_size, attribute_size;
} DxilExport;

enum {
    DXIL_KIND_RAYGEN = 7,
    DXIL_KIND_INTERSECTION,
    DXIL_KIND_ANYHIT,
    DXIL_KIND_CLOSESTHIT,
    DXIL_KIND_MISS,
    DXIL_KIND_CALLABLE,
};

/* writes up to max exports and returns their count, -1 when the container holds no library. */
int dxil_library_exports(const void *bytecode, size_t length, DxilExport *out, uint32_t max,
                         const MimeAllocator *alloc);
/* matches mangled or plain function names. */
int dxil_export_matches(const char *function, const char *export_name);

#endif
