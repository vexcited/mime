/* the compiler from dxbc to spir-v. */
#include "dxbc.h"

int dxbc_compile(const void *bytecode, size_t length, const DxbcCompileOptions *options, DxbcShaderInfo *info,
                 const MimeAllocator *alloc)
{
    __builtin_memset(info, 0, sizeof(*info));
    __builtin_memcpy(info->error, "not implemented", 16);
    return -1;
}

void dxbc_free(DxbcShaderInfo *info, const MimeAllocator *alloc)
{
    mime_free(alloc, info->inputs.elements);
    mime_free(alloc, info->outputs.elements);
    mime_free(alloc, info->patch_constants.elements);
    mime_free(alloc, info->spirv);
    __builtin_memset(info, 0, sizeof(*info));
}
