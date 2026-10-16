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

/* compiles the shader; 0 on success, the reason in info->error otherwise. free with dxbc_free. */
int dxil_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc);

#endif
