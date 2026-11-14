#ifndef MIME_GPU_ARCHIVE_H
#define MIME_GPU_ARCHIVE_H

#include <objc/objc.h>

/*
 * Metal 4 builds libraries and pipelines through a compiler. the system keeps what a compiler builds in its shader
 * cache, where the next run finds a pipeline in about two milliseconds instead of tens, unless the compiler captures
 * its pipelines for an archive: then it only reads that cache. an archive is no faster to look up and only holds what
 * was captured since it was last written, so the compilers here capture nothing. one compiler per name, for the life
 * of the process.
 */
typedef struct MimePipelineCache MimePipelineCache;

MimePipelineCache *mime_pipeline_cache(id device, const char *name);

/*
 * each builds from an MTL4LibraryDescriptor or an MTL4PipelineDescriptor and returns a retained object, or null with
 * the error. builds from several threads run together.
 */
void *mime_pipeline_cache_library(MimePipelineCache *cache, id descriptor, id *error);
void *mime_pipeline_cache_render(MimePipelineCache *cache, id descriptor, id *error);
void *mime_pipeline_cache_compute(MimePipelineCache *cache, id descriptor, id *error);

#endif
