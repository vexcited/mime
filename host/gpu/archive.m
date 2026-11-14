#include "archive.h"

#import <Metal/Metal.h>
#include <os/lock.h>

MimePipelineCache *mime_pipeline_cache(id device, const char *name)
{
    static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
    static NSMutableDictionary<NSString *, id<MTL4Compiler>> *compilers;
    @autoreleasepool {
        NSString *key = @(name);
        os_unfair_lock_lock(&lock);
        if (!compilers)
            compilers = [[NSMutableDictionary alloc] init];
        id<MTL4Compiler> compiler = compilers[key];
        if (!compiler) {
            MTL4CompilerDescriptor *descriptor = [[MTL4CompilerDescriptor alloc] init];
            descriptor.label = key;
            compiler = [device newCompilerWithDescriptor:descriptor error:nil];
            if (compiler)
                compilers[key] = compiler;
        }
        os_unfair_lock_unlock(&lock);
        return (__bridge MimePipelineCache *)compiler;
    }
}

void *mime_pipeline_cache_library(MimePipelineCache *cache, id descriptor, id *error)
{
    NSError *failure = nil;
    id made = [(__bridge id<MTL4Compiler>)cache newLibraryWithDescriptor:descriptor error:&failure];
    if (error)
        *error = failure;
    return (__bridge_retained void *)made;
}

void *mime_pipeline_cache_render(MimePipelineCache *cache, id descriptor, id *error)
{
    NSError *failure = nil;
    id made = [(__bridge id<MTL4Compiler>)cache newRenderPipelineStateWithDescriptor:descriptor
                                                                 compilerTaskOptions:nil
                                                                               error:&failure];
    if (error)
        *error = failure;
    return (__bridge_retained void *)made;
}

void *mime_pipeline_cache_compute(MimePipelineCache *cache, id descriptor, id *error)
{
    NSError *failure = nil;
    id made = [(__bridge id<MTL4Compiler>)cache newComputePipelineStateWithDescriptor:descriptor
                                                                  compilerTaskOptions:nil
                                                                                error:&failure];
    if (error)
        *error = failure;
    return (__bridge_retained void *)made;
}
