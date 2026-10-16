/*
 * dxil: the shader model 6 bytecode of direct3d 12, an llvm 3.7 module in
 * a dxbc container, compiled to spir-v with the same binding model, the
 * same options and the same results as dxbc_compile.
 */
#ifndef MIME_LIB_DXIL_H
#define MIME_LIB_DXIL_H

#include "../dxbc/dxbc.h"

/* whether the container holds dxil rather than dxbc. */
int dxil_is(const void *bytecode, size_t length);

/*
 * compiles the shader, or the function of a library options->export_name
 * names; 0 on success, the reason in info->error otherwise. free with
 * dxbc_free.
 */
int dxil_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc);

/* a function a library exports: its name, its shader kind as dxil numbers them, its payload and attributes' sizes. */
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

/* the functions of a library, up to max of them written; their count, -1 when the container holds no library. */
int dxil_library_exports(const void *bytecode, size_t length, DxilExport *out, uint32_t max,
                         const MimeAllocator *alloc);
/* whether a function's name, mangled or not, is the name it is exported by. */
int dxil_export_matches(const char *function, const char *export_name);

#endif
